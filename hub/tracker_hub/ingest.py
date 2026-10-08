"""MQTT ingestion and everything derived from it.

Wire format: car-tracker docs/05-protokol-mqtt.md. Both the verbose and the
compact key sets are accepted, because the device switches to compact keys to
stay inside the data budget.

Design rule: this module is the only place that decides what a message means.
The store just persists, the web layer just renders.
"""

from __future__ import annotations

import json
import logging
import math
import ssl
import threading
import time
from collections import deque
from typing import Any

import paho.mqtt.client as mqtt

from .config import Config
from .discovery import Discovery
from .store import Store

_LOG = logging.getLogger(__name__)


def _new_client(client_id: str) -> mqtt.Client:
    """Construct a client on either paho 2.x (apt on Debian 13) or 1.x."""
    if hasattr(mqtt, "CallbackAPIVersion"):
        return mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
    return mqtt.Client(client_id=client_id)


# Compact key map, mirrors packet.cpp fillPos() in the car-tracker firmware.
_COMPACT = {
    "q": "seq", "t": "ts", "a": "lat", "o": "lon", "s": "speed",
    "c": "course", "n": "sat", "h": "hdop", "m": "mode", "r": "src",
}
_MODES = ["parked", "driving", "moved", "hibernate"]
_SOURCES = ["neo6m", "modem"]


def haversine_km(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    radius = 6371.0088
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp = math.radians(lat2 - lat1)
    dl = math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * radius * math.asin(math.sqrt(a))


def normalise_position(raw: dict[str, Any]) -> dict[str, Any]:
    """Accept both wire formats and return one shape."""
    if "lat" in raw or "lon" in raw:
        out = dict(raw)
        # The verbose format spells speed "spd" and mode "st".
        if "spd" in out:
            out["speed"] = out.pop("spd")
        if "crs" in out:
            out["course"] = out.pop("crs")
        if "st" in out:
            out["mode"] = out.pop("st")
    else:
        out = {long: raw[short] for short, long in _COMPACT.items() if short in raw}

    if isinstance(out.get("mode"), int):
        idx = out["mode"]
        out["mode"] = _MODES[idx] if 0 <= idx < len(_MODES) else "parked"
    if isinstance(out.get("src"), int):
        idx = out["src"]
        out["src"] = _SOURCES[idx] if 0 <= idx < len(_SOURCES) else "neo6m"
    return out


class Ingest:
    """Owns the MQTT connection and turns messages into stored state."""

    def __init__(self, cfg: Config, store: Store) -> None:
        self.cfg = cfg
        self.store = store
        self.connected = False
        self.last_message_at: float | None = None
        self.messages = 0
        self.dropped = 0
        self._lock = threading.Lock()

        # Admin view only, not history: the last config the broker holds for
        # each vehicle, its last info packet and the recent command acks.
        self.last_cfg: dict[str, dict[str, Any]] = {}
        self.last_info: dict[str, dict[str, Any]] = {}
        self.acks: dict[str, deque[dict[str, Any]]] = {}
        self._client = _new_client("tracker-hub")
        self._client.username_pw_set(cfg.mqtt_user, cfg.mqtt_pass)
        if cfg.mqtt_tls:
            if cfg.mqtt_ca:
                self._client.tls_set(ca_certs=cfg.mqtt_ca, cert_reqs=ssl.CERT_REQUIRED)
            else:
                self._client.tls_set(cert_reqs=ssl.CERT_REQUIRED)
        self._client.on_connect = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_message = self._on_message

        self.discovery = Discovery(self._raw_publish, cfg.topic_prefix,
                                   cfg.discovery_prefix)

    # --- lifecycle --------------------------------------------------------

    def start(self) -> None:
        self._client.connect_async(self.cfg.mqtt_host, self.cfg.mqtt_port, keepalive=60)
        self._client.loop_start()  # paho reconnects on its own

    def stop(self) -> None:
        self._client.loop_stop()
        self._client.disconnect()

    def _raw_publish(self, topic: str, payload: str, retain: bool = False) -> None:
        self._client.publish(topic, payload, qos=1, retain=retain)

    def announce_all(self, force: bool = False) -> int:
        """Re-announce every known vehicle. Called on connect."""
        if not self.cfg.discovery:
            return 0
        total = 0
        for vehicle in self.store.vehicles():
            total += self.discovery.announce(vehicle, force=force)
            self.discovery.publish_trip(
                vehicle["vehicle_id"], self.store.open_trip(vehicle["vehicle_id"]),
                time.time(),
            )
        return total

    def _announce(self, vehicle_id: str) -> None:
        if not self.cfg.discovery:
            return
        vehicle = self.store.vehicle(vehicle_id)
        if vehicle:
            self.discovery.announce(vehicle)

    def _push_trip(self, vehicle_id: str) -> None:
        if not self.cfg.discovery:
            return
        self.discovery.publish_trip(
            vehicle_id, self.store.open_trip(vehicle_id), time.time()
        )

    def publish_command(self, vehicle_id: str, command: str, **kwargs: Any) -> bool:
        payload = {"id": f"{int(time.time()) & 0xFFFF:04x}", "cmd": command, **kwargs}
        topic = f"{self.cfg.topic_prefix}/{vehicle_id}/cmd"
        # Never retained: a retained reboot would replay on every reconnect.
        info = self._client.publish(topic, json.dumps(payload), qos=1, retain=False)
        return info.rc == mqtt.MQTT_ERR_SUCCESS

    def publish_config(self, vehicle_id: str, config: dict[str, Any]) -> bool:
        topic = f"{self.cfg.topic_prefix}/{vehicle_id}/cfg"
        info = self._client.publish(topic, json.dumps(config), qos=1, retain=True)
        return info.rc == mqtt.MQTT_ERR_SUCCESS

    def health(self) -> dict[str, Any]:
        return {
            "connected": self.connected,
            "messages": self.messages,
            "dropped": self.dropped,
            "last_message_at": self.last_message_at,
        }

    # --- MQTT callbacks ---------------------------------------------------

    # Callback signatures differ between paho 1.x and 2.x (2.x adds a properties
    # argument and passes a ReasonCode instead of an int). Accepting *args keeps
    # one code path for the apt package on Debian 13 (2.1) and the 1.6 that is
    # still common on workstations.
    def _on_connect(self, client, userdata, flags, *args):
        reason_code = args[0] if args else 0
        # paho 2.x hands over a ReasonCode object, 1.x a plain int. ReasonCode
        # is not int()-able, so read its .value and fall back to the int.
        code = getattr(reason_code, "value", reason_code)
        if code != 0:
            _LOG.error("MQTT connect refused: %s", reason_code)
            return
        self.connected = True
        topic = f"{self.cfg.topic_prefix}/+/#"
        client.subscribe(topic, qos=1)
        _LOG.info("MQTT connected, subscribed to %s", topic)
        # Retained configs may have been cleared on the broker, and this is the
        # only moment we know the connection is usable.
        self.announce_all(force=True)

    def _on_disconnect(self, client, userdata, *args):
        self.connected = False
        _LOG.warning("MQTT disconnected: %s", args[-1] if args else "unknown")

    def _on_message(self, client, userdata, msg):
        try:
            self._handle(msg.topic, msg.payload, retained=bool(msg.retain))
        except Exception:  # noqa: BLE001 - one bad message must not kill ingestion
            _LOG.exception("failed to handle %s", msg.topic)

    # --- routing ----------------------------------------------------------

    def _handle(self, topic: str, payload: bytes, retained: bool = False) -> None:
        """`retained` means the broker replayed this on connect, not that the
        device just said it. The device retains its last position and telemetry
        so a restarted Home Assistant is not blind until the next packet, and
        that replay must not be mistaken for something happening now."""
        parts = topic.split("/")
        if len(parts) < 3 or parts[0] != self.cfg.topic_prefix:
            return
        vehicle_id, kind = parts[1], parts[2]
        self.messages += 1
        self.last_message_at = time.time()

        if kind == "status":
            self._on_status(vehicle_id, payload)
            return

        data = self._decode(topic, payload)
        if data is None:
            return

        if kind == "pos":
            self._on_position(vehicle_id, normalise_position(data), retained=retained)
        elif kind == "batch":
            self._on_batch(vehicle_id, data)
        elif kind == "tel":
            self._on_telemetry(vehicle_id, data, retained=retained)
        elif kind == "evt":
            self._on_event(vehicle_id, data)
        elif kind == "info":
            self._on_info(vehicle_id, data)
        elif kind == "cfg":
            self.last_cfg[vehicle_id] = data
        elif kind == "ack":
            self.acks.setdefault(vehicle_id, deque(maxlen=20)).appendleft(
                {**data, "received": time.time()}
            )

    def _decode(self, topic: str, payload: bytes) -> dict[str, Any] | None:
        try:
            data = json.loads(payload)
        except (ValueError, TypeError):
            _LOG.warning("undecodable payload on %s", topic)
            self.dropped += 1
            return None
        if not isinstance(data, dict):
            _LOG.warning("payload on %s is not an object", topic)
            self.dropped += 1
            return None
        return data

    # --- handlers ---------------------------------------------------------

    def _on_status(self, vehicle_id: str, payload: bytes) -> None:
        online = payload.decode("utf-8", "replace").strip().lower() == "online"
        self.store.upsert_vehicle(
            vehicle_id, online=1 if online else 0, last_status=time.time(),
            last_seen=time.time(),
        )
        _LOG.info("%s is %s", vehicle_id, "online" if online else "offline")

    def _on_info(self, vehicle_id: str, data: dict[str, Any]) -> None:
        # Identity comes from the device, so a car is labelled by the board
        # bolted into it and there is no second registry to keep in sync.
        self.last_info[vehicle_id] = data
        self.store.upsert_vehicle(
            vehicle_id, firmware=data.get("fw"), modem=data.get("modem"),
            imei=data.get("imei"), ip=data.get("ip"), name=data.get("name"),
            plate=data.get("plate"), vin=data.get("vin"), last_seen=time.time(),
        )
        self._announce(vehicle_id)

    def _on_telemetry(self, vehicle_id: str, data: dict[str, Any],
                      retained: bool = False) -> None:
        self._announce(vehicle_id)
        ts = float(data.get("ts") or time.time())
        # A replayed retained packet is a row we already have. Only the vehicle
        # summary below is refreshed, so the fleet page still shows the last
        # known voltage after a hub restart.
        if not retained:
            self.store.insert_telemetry(
                vehicle_id, ts, data.get("vbat"), data.get("rssi"), data.get("q"),
                data.get("st"),
            )
        self.store.upsert_vehicle(
            vehicle_id, voltage=data.get("vbat"), rssi=data.get("rssi"),
            network=data.get("net"), operator=data.get("op"),
            roaming=1 if data.get("roam") else 0, queued=data.get("q"),
            uptime=data.get("up"), mode=data.get("st"), ip=data.get("ip"),
            last_seen=time.time(),
        )

    def _on_event(self, vehicle_id: str, data: dict[str, Any]) -> None:
        event = data.get("ev")
        if not event:
            return
        ts = float(data.get("ts") or time.time())
        lat, lon = data.get("lat"), data.get("lon")
        self.store.insert_event(vehicle_id, ts, event, lat, lon, json.dumps(data))
        self.store.upsert_vehicle(vehicle_id, last_seen=time.time())

        if event == "trip_start":
            self.store.start_trip(vehicle_id, ts, lat, lon)
            _LOG.info("%s trip started", vehicle_id)
            self._push_trip(vehicle_id)
        elif event == "trip_end":
            trip = self.store.open_trip(vehicle_id)
            if trip:
                self.store.update_trip(
                    trip["id"], ended=ts, end_lat=lat, end_lon=lon, open=0
                )
                _LOG.info(
                    "%s trip %d ended: %.1f km", vehicle_id, trip["id"],
                    trip["distance_km"],
                )
                # Pushed after the update so the retained payload holds the
                # finished trip, which is the answer to "how far last time".
                if self.cfg.discovery:
                    self.discovery.publish_trip(
                        vehicle_id, self.store.trip(trip["id"]), time.time()
                    )
        elif event == "motion_alarm":
            _LOG.warning("%s MOTION ALARM at %s,%s", vehicle_id, lat, lon)

    def _on_batch(self, vehicle_id: str, data: dict[str, Any]) -> None:
        """Backlog flushed after the link came back."""
        points = data.get("pts") or []
        if not isinstance(points, list):
            return
        ordered = sorted(points, key=lambda p: p.get("t") or p.get("ts") or 0)
        stored = 0
        for raw in ordered:
            if isinstance(raw, dict) and self._on_position(
                vehicle_id, normalise_position(raw), historic=True
            ):
                stored += 1
        _LOG.info("%s backlog: %d points, %d new", vehicle_id, len(ordered), stored)

    def _on_position(self, vehicle_id: str, pos: dict[str, Any],
                     historic: bool = False, retained: bool = False) -> bool:
        lat, lon = pos.get("lat"), pos.get("lon")
        if lat is None or lon is None:
            self.dropped += 1
            return False

        hdop = pos.get("hdop")
        if hdop is not None and hdop > self.cfg.max_hdop:
            self.dropped += 1
            return False

        ts = float(pos.get("ts") or 0)
        if not ts:
            # A live point without a timestamp is close to "now"; one replayed
            # from the offline queue is not, and stamping it with the arrival
            # time would move it hours and feed the teleport guard a lie.
            if historic:
                _LOG.warning("%s: backlog point seq=%s has no timestamp, dropped",
                             vehicle_id, pos.get("seq"))
                self.dropped += 1
                return False
            ts = time.time()
        pos["ts"] = ts

        with self._lock:
            previous = self.store.last_position(vehicle_id)

            # Teleport guard: a jump needing an impossible speed is a GNSS
            # glitch, not a position. Only applies with a sane time delta.
            if previous and ts > previous["ts"]:
                dt_h = (ts - previous["ts"]) / 3600.0
                if dt_h > 0:
                    km = haversine_km(previous["lat"], previous["lon"], lat, lon)
                    if km / dt_h > self.cfg.max_plausible_speed_kmh:
                        _LOG.warning(
                            "%s: implausible jump %.1f km in %.0f s, dropped",
                            vehicle_id, km, dt_h * 3600,
                        )
                        self.dropped += 1
                        return False

            trip = self.store.open_trip(vehicle_id)
            # A driving position with no open trip means the trip_start event was
            # lost (no coverage at the moment it fired). Open one from here rather
            # than throwing the drive away.
            #
            # Not from a retained one, though: that is the broker replaying the
            # last drive on every reconnect, and it would open a one sample trip
            # for a car that has been parked since.
            if trip is None and pos.get("mode") == "driving" and not retained:
                trip_id = self.store.start_trip(vehicle_id, ts, lat, lon)
                trip = self.store.trip(trip_id)
                _LOG.info("%s trip %d opened from a position, no trip_start seen",
                          vehicle_id, trip_id)

            trip_id = trip["id"] if trip else None
            inserted = self.store.insert_position(vehicle_id, pos, trip_id)
            if not inserted:
                return False  # duplicate from a re-flushed backlog

            if trip is not None:
                self._accumulate_trip(trip, previous, pos)

            self.store.upsert_vehicle(
                vehicle_id, mode=pos.get("mode"), last_seen=time.time()
            )
            trip_after = trip is not None

        # Outside the lock: publishing must not hold up ingestion, and the trip
        # sensors are the only entities that need the hub in their path at all.
        if trip_after and not historic:
            self._push_trip(vehicle_id)
        return True

    def _accumulate_trip(self, trip: dict[str, Any], previous: dict[str, Any] | None,
                         pos: dict[str, Any]) -> None:
        distance = trip["distance_km"]
        if previous is not None:
            distance += haversine_km(
                previous["lat"], previous["lon"], pos["lat"], pos["lon"]
            )
        speed = float(pos.get("speed") or 0.0)
        samples = trip["samples"] + 1
        avg = (trip["avg_speed"] * trip["samples"] + speed) / samples
        self.store.update_trip(
            trip["id"],
            distance_km=distance,
            max_speed=max(trip["max_speed"], speed),
            avg_speed=avg,
            samples=samples,
        )
