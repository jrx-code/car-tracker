"""MQTT plumbing and the derived state (trips, dedup, plausibility).

Everything above raw samples is computed here and not in the firmware, so it can
be fixed without pulling the device out of the car (docs/02 section 2.8).
"""

from __future__ import annotations

import json
import logging
import math
import time
from dataclasses import dataclass, field
from typing import Any, Callable

from homeassistant.components import mqtt
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.dispatcher import async_dispatcher_send

from .const import (
    DOMAIN,
    MAX_HDOP,
    MAX_PLAUSIBLE_SPEED_KMH,
    MODE_DRIVING,
    SIGNAL_EVENT,
    SIGNAL_POSITION,
    SIGNAL_TELEMETRY,
    TOPIC_ACK,
    TOPIC_BATCH,
    TOPIC_CFG,
    TOPIC_CMD,
    TOPIC_EVT,
    TOPIC_INFO,
    TOPIC_POS,
    TOPIC_STATUS,
    TOPIC_TEL,
)

_LOGGER = logging.getLogger(__name__)

# Compact-mode key map, mirrors packet.cpp fillPos().
_COMPACT_KEYS = {
    "q": "seq",
    "t": "ts",
    "a": "lat",
    "o": "lon",
    "s": "spd",
    "c": "crs",
    "n": "sat",
    "h": "hdop",
    "m": "st",
    "r": "src",
}

_MODE_BY_INDEX = ["parked", "driving", "moved", "hibernate"]
_SRC_BY_INDEX = ["neo6m", "modem"]


def _haversine_km(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Great circle distance in kilometres."""
    radius = 6371.0088
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp = math.radians(lat2 - lat1)
    dl = math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * radius * math.asin(math.sqrt(a))


@dataclass
class Trip:
    """One journey, accumulated from positions between trip_start and trip_end."""

    started: float
    start_lat: float
    start_lon: float
    distance_km: float = 0.0
    max_speed: float = 0.0
    speed_sum: float = 0.0
    samples: int = 0
    ended: float | None = None
    end_lat: float | None = None
    end_lon: float | None = None

    @property
    def duration_s(self) -> float:
        return (self.ended or time.time()) - self.started

    @property
    def avg_speed(self) -> float:
        return self.speed_sum / self.samples if self.samples else 0.0


@dataclass
class TrackerData:
    """Latest known state of one vehicle."""

    online: bool = False
    mode: str = "parked"
    lat: float | None = None
    lon: float | None = None
    alt: float | None = None
    speed: float = 0.0
    course: int = 0
    satellites: int = 0
    hdop: float | None = None
    gnss_source: str | None = None
    position_ts: float | None = None
    position_ts_source: str | None = None
    voltage: float | None = None
    rssi: int | None = None
    network: str | None = None
    operator: str | None = None
    roaming: bool = False
    queued: int = 0
    uptime: int = 0
    reset_reason: int | None = None
    firmware: str | None = None
    modem: str | None = None
    imei: str | None = None
    last_event: str | None = None
    last_event_ts: float | None = None
    trip: Trip | None = None
    last_trip: Trip | None = None
    seen_seq: set[int] = field(default_factory=set)


class CarTrackerCoordinator:
    """Subscribes to one vehicle's topics and keeps its derived state."""

    def __init__(
        self, hass: HomeAssistant, entry_id: str, prefix: str, vehicle_id: str
    ) -> None:
        self.hass = hass
        self.entry_id = entry_id
        self.vehicle_id = vehicle_id
        self._base = f"{prefix}/{vehicle_id}"
        self.data = TrackerData()
        self._unsubs: list[Callable[[], None]] = []

    # --- lifecycle --------------------------------------------------------

    async def async_setup(self) -> None:
        for sub, handler in (
            (TOPIC_STATUS, self._on_status),
            (TOPIC_INFO, self._on_info),
            (TOPIC_POS, self._on_pos),
            (TOPIC_TEL, self._on_tel),
            (TOPIC_EVT, self._on_evt),
            (TOPIC_BATCH, self._on_batch),
            (TOPIC_ACK, self._on_ack),
        ):
            self._unsubs.append(
                await mqtt.async_subscribe(self.hass, f"{self._base}/{sub}", handler, 1)
            )

    async def async_unload(self) -> None:
        for unsub in self._unsubs:
            unsub()
        self._unsubs.clear()

    # --- outgoing ---------------------------------------------------------

    async def async_send_command(self, command: str, **kwargs: Any) -> None:
        payload = {"id": f"{int(time.time()) & 0xFFFF:04x}", "cmd": command, **kwargs}
        # Never retained: a retained reboot command would replay on every
        # reconnect and loop the device (docs/05 section 5.7).
        await mqtt.async_publish(
            self.hass, f"{self._base}/{TOPIC_CMD}", json.dumps(payload), 1, False
        )

    async def async_publish_config(self, config: dict[str, Any]) -> None:
        await mqtt.async_publish(
            self.hass, f"{self._base}/{TOPIC_CFG}", json.dumps(config), 1, True
        )

    # --- incoming ---------------------------------------------------------

    @callback
    def _on_status(self, msg: mqtt.ReceiveMessage) -> None:
        self.data.online = msg.payload.strip().lower() == "online"
        self._notify(SIGNAL_TELEMETRY)

    @callback
    def _on_info(self, msg: mqtt.ReceiveMessage) -> None:
        data = self._decode(msg.payload)
        if data is None:
            return
        self.data.firmware = data.get("fw")
        self.data.modem = data.get("modem")
        self.data.imei = data.get("imei")
        self._notify(SIGNAL_TELEMETRY)

    @callback
    def _on_pos(self, msg: mqtt.ReceiveMessage) -> None:
        data = self._decode(msg.payload)
        if data is None:
            return
        if self._apply_position(self._normalise(data)):
            self._notify(SIGNAL_POSITION)

    @callback
    def _on_batch(self, msg: mqtt.ReceiveMessage) -> None:
        """Backlog flushed after the link came back (docs/02 section 2.6)."""
        data = self._decode(msg.payload)
        if data is None:
            return
        points = data.get("pts") or []
        changed = False
        for raw in sorted(points, key=lambda p: p.get("t") or p.get("ts") or 0):
            changed |= self._apply_position(self._normalise(raw), historic=True)
        if changed:
            self._notify(SIGNAL_POSITION)

    @callback
    def _on_tel(self, msg: mqtt.ReceiveMessage) -> None:
        data = self._decode(msg.payload)
        if data is None:
            return
        d = self.data
        d.voltage = data.get("vbat", d.voltage)
        d.rssi = data.get("rssi", d.rssi)
        d.network = data.get("net", d.network)
        d.operator = data.get("op", d.operator)
        d.roaming = bool(data.get("roam", d.roaming))
        d.queued = int(data.get("q", d.queued) or 0)
        d.uptime = int(data.get("up", d.uptime) or 0)
        d.reset_reason = data.get("rst", d.reset_reason)
        if (mode := data.get("st")) is not None:
            d.mode = mode
        self._notify(SIGNAL_TELEMETRY)

    @callback
    def _on_evt(self, msg: mqtt.ReceiveMessage) -> None:
        data = self._decode(msg.payload)
        if data is None:
            return
        event = data.get("ev")
        if not event:
            return
        self.data.last_event = event
        self.data.last_event_ts = data.get("ts") or time.time()

        if event == "trip_start":
            self.data.trip = Trip(
                started=self.data.last_event_ts,
                start_lat=data.get("lat", self.data.lat or 0.0),
                start_lon=data.get("lon", self.data.lon or 0.0),
            )
        elif event == "trip_end" and self.data.trip is not None:
            trip = self.data.trip
            trip.ended = self.data.last_event_ts
            trip.end_lat = data.get("lat", self.data.lat)
            trip.end_lon = data.get("lon", self.data.lon)
            self.data.last_trip = trip
            self.data.trip = None

        self.hass.bus.async_fire(
            f"{DOMAIN}_event",
            {"vehicle_id": self.vehicle_id, "event": event, **data},
        )
        self._notify(SIGNAL_EVENT)

    @callback
    def _on_ack(self, msg: mqtt.ReceiveMessage) -> None:
        data = self._decode(msg.payload)
        if data is not None:
            _LOGGER.debug("%s ack: %s", self.vehicle_id, data)

    # --- helpers ----------------------------------------------------------

    def _decode(self, payload: Any) -> dict[str, Any] | None:
        try:
            data = json.loads(payload)
        except (ValueError, TypeError):
            _LOGGER.warning("%s: undecodable payload %r", self.vehicle_id, payload)
            return None
        if not isinstance(data, dict):
            _LOGGER.warning("%s: payload is not an object", self.vehicle_id)
            return None
        return data

    @staticmethod
    def _normalise(raw: dict[str, Any]) -> dict[str, Any]:
        """Accept both the verbose and the compact wire format."""
        if "lat" in raw or "lon" in raw:
            return raw
        out: dict[str, Any] = {}
        for short, long in _COMPACT_KEYS.items():
            if short in raw:
                out[long] = raw[short]
        if isinstance(out.get("st"), int):
            idx = out["st"]
            out["st"] = _MODE_BY_INDEX[idx] if 0 <= idx < len(_MODE_BY_INDEX) else "parked"
        if isinstance(out.get("src"), int):
            idx = out["src"]
            out["src"] = _SRC_BY_INDEX[idx] if 0 <= idx < len(_SRC_BY_INDEX) else "neo6m"
        return out

    def _apply_position(self, data: dict[str, Any], historic: bool = False) -> bool:
        lat, lon = data.get("lat"), data.get("lon")
        if lat is None or lon is None:
            return False

        seq = data.get("seq")
        if seq is not None:
            # Duplicates are expected: a record leaves the queue only after
            # PUBACK, so a power cut mid-flush resends it (docs/02 section 2.6).
            if seq in self.data.seen_seq:
                return False
            self.data.seen_seq.add(seq)
            if len(self.data.seen_seq) > 5000:
                self.data.seen_seq = set(sorted(self.data.seen_seq)[-2500:])

        hdop = data.get("hdop")
        if hdop is not None and hdop > MAX_HDOP:
            _LOGGER.debug("%s: dropped fix, hdop %s", self.vehicle_id, hdop)
            return False

        ts = data.get("ts") or time.time()
        d = self.data

        # Teleport guard: a jump that would need an impossible speed is a GNSS
        # glitch, not a position. Only applied when there is a previous fix and
        # a sane time delta.
        if d.lat is not None and d.position_ts and ts > d.position_ts:
            dt_h = (ts - d.position_ts) / 3600.0
            if dt_h > 0:
                km = _haversine_km(d.lat, d.lon, lat, lon)
                if km / dt_h > MAX_PLAUSIBLE_SPEED_KMH:
                    _LOGGER.warning(
                        "%s: implausible jump %.1f km in %.0f s, dropped",
                        self.vehicle_id,
                        km,
                        dt_h * 3600,
                    )
                    return False

        if d.trip is not None and d.lat is not None:
            d.trip.distance_km += _haversine_km(d.lat, d.lon, lat, lon)
            speed = float(data.get("spd") or 0.0)
            d.trip.max_speed = max(d.trip.max_speed, speed)
            d.trip.speed_sum += speed
            d.trip.samples += 1

        # A backlog point older than what we already show must not drag the
        # tracker backwards on the map; it counts towards the trip and stops there.
        if historic and d.position_ts and ts < d.position_ts:
            return False

        d.lat, d.lon = lat, lon
        d.alt = data.get("alt", d.alt)
        d.speed = float(data.get("spd") or 0.0)
        d.course = int(data.get("crs") or 0)
        d.satellites = int(data.get("sat") or 0)
        d.hdop = hdop
        d.gnss_source = data.get("src")
        d.position_ts = ts
        d.position_ts_source = data.get("ts_src")
        if (mode := data.get("st")) is not None:
            d.mode = mode
        return True

    @callback
    def _notify(self, signal: str) -> None:
        async_dispatcher_send(self.hass, f"{signal}_{self.entry_id}")

    @property
    def is_driving(self) -> bool:
        return self.data.mode == MODE_DRIVING
