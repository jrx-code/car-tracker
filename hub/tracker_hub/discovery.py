"""Home Assistant MQTT discovery.

The hub owns the derived data, so it is also the thing that tells Home Assistant
what entities exist. What it does NOT do is stand in the middle of every reading:
most entities point their state topic straight at the device's own topics, so a
dead hub costs nothing but the trip sensors. Battery voltage and the motion alarm
never pass through this process (car-tracker docs/08 section 8.3).

Configs are retained, so entities survive a hub restart and a hub that never
comes back. They are republished when the identity a device reports changes,
because the device name and VIN live in the device block.
"""

from __future__ import annotations

import json
import logging
from typing import Any

_LOG = logging.getLogger(__name__)

# Bumped when the entity set changes, so a running HA picks up the new shape
# instead of keeping whatever the previous version registered.
LAYOUT_VERSION = 2

# HDOP is not metres. The nominal receiver accuracy turns it into a radius:
# about 2.5 m for the GPS-only NEO-6M (car-tracker docs/03 section 3.1).
GNSS_BASE_M = 2.5

MODE_LABELS = ["parked", "driving", "moved", "hibernate"]


def _sensor(key: str, name: str, source: str, template: str, **extra: Any) -> dict:
    """One sensor description. `source` is a topic suffix on the device."""
    out = {
        "key": key,
        "component": "sensor",
        "source": source,
        "config": {"name": name, "value_template": template, **extra},
    }
    return out


# Entities fed straight from the device. The hub is not in this path.
DIRECT: list[dict] = [
    _sensor("voltage", "Napięcie akumulatora", "tel", "{{ value_json.vbat }}",
            device_class="voltage", unit_of_measurement="V", state_class="measurement",
            suggested_display_precision=2),
    _sensor("speed", "Prędkość", "pos", "{{ value_json.spd }}",
            device_class="speed", unit_of_measurement="km/h", state_class="measurement",
            suggested_display_precision=0),
    _sensor("satellites", "Satelity", "pos", "{{ value_json.sat }}",
            state_class="measurement", entity_category="diagnostic",
            icon="mdi:satellite-variant"),
    _sensor("hdop", "HDOP", "pos", "{{ value_json.hdop }}",
            state_class="measurement", entity_category="diagnostic",
            icon="mdi:crosshairs-gps", suggested_display_precision=1),
    _sensor("rssi", "Siła sygnału", "tel", "{{ value_json.rssi }}",
            device_class="signal_strength", unit_of_measurement="dBm",
            state_class="measurement", entity_category="diagnostic"),
    _sensor("network", "Sieć", "tel", "{{ value_json.net }}",
            entity_category="diagnostic", icon="mdi:radio-tower"),
    # The backlog depth is the earliest warning that the link is degrading, which
    # is why it is a first class entity and not an attribute.
    _sensor("queued", "Punkty w kolejce", "tel", "{{ value_json.q }}",
            state_class="measurement", entity_category="diagnostic",
            icon="mdi:tray-full"),
    _sensor("uptime", "Czas pracy", "tel", "{{ value_json.up }}",
            device_class="duration", unit_of_measurement="s",
            entity_category="diagnostic"),
    _sensor("mode", "Tryb", "tel", "{{ value_json.st }}",
            device_class="enum", options=MODE_LABELS, icon="mdi:car-info"),
]

# Binary sensors, also straight from the device. `payload_on`/`payload_off` are
# not usable here because the source is a JSON field, so the template renders
# the two literals itself.
DIRECT_BINARY: list[dict] = [
    {
        "key": "moving", "component": "binary_sensor", "source": "tel",
        "config": {
            "name": "Jazda", "device_class": "moving",
            "value_template": "{{ 'ON' if value_json.st == 'driving' else 'OFF' }}",
        },
    },
    {
        "key": "tow_alarm", "component": "binary_sensor", "source": "tel",
        "config": {
            "name": "Ruch na postoju", "device_class": "problem",
            "value_template": "{{ 'ON' if value_json.st == 'moved' else 'OFF' }}",
        },
    },
    {
        "key": "hibernate", "component": "binary_sensor", "source": "tel",
        "config": {
            "name": "Hibernacja", "entity_category": "diagnostic",
            "icon": "mdi:sleep",
            "value_template": "{{ 'ON' if value_json.st == 'hibernate' else 'OFF' }}",
        },
    },
    {
        "key": "battery_low", "component": "binary_sensor", "source": "tel",
        "config": {
            "name": "Niskie napięcie", "device_class": "battery",
            # The threshold lives here rather than in the device because it is a
            # display decision; the device has its own, lower, hibernation limit.
            "value_template":
                "{{ 'ON' if (value_json.vbat | float(99)) < 12.2 else 'OFF' }}",
        },
    },
    {
        "key": "roaming", "component": "binary_sensor", "source": "tel",
        "config": {
            "name": "Roaming", "entity_category": "diagnostic",
            "icon": "mdi:earth",
            "value_template": "{{ 'ON' if value_json.roam else 'OFF' }}",
        },
    },
]

# Derived, and therefore hub-published. These are the only entities that go
# stale when the hub stops.
DERIVED: list[dict] = [
    _sensor("trip_distance", "Dystans przejazdu", "trip",
            "{{ value_json.distance_km }}", device_class="distance",
            unit_of_measurement="km", state_class="measurement",
            suggested_display_precision=1),
    _sensor("trip_duration", "Czas przejazdu", "trip", "{{ value_json.duration_s }}",
            device_class="duration", unit_of_measurement="s",
            state_class="measurement"),
    _sensor("trip_max_speed", "Prędkość maksymalna", "trip",
            "{{ value_json.max_speed }}", device_class="speed",
            unit_of_measurement="km/h", state_class="measurement",
            suggested_display_precision=0),
    _sensor("trip_avg_speed", "Prędkość średnia", "trip",
            "{{ value_json.avg_speed }}", device_class="speed",
            unit_of_measurement="km/h", state_class="measurement",
            suggested_display_precision=0),
]

# Buttons publish to the command topic. Commands are never retained: a retained
# reboot would replay on every reconnect and loop the device.
BUTTONS: list[dict] = [
    {"key": "locate", "name": "Zlokalizuj teraz", "icon": "mdi:crosshairs-gps"},
    {"key": "ping", "name": "Ping", "icon": "mdi:lan-pending"},
]


class Discovery:
    def __init__(self, publish, topic_prefix: str, discovery_prefix: str) -> None:
        self._publish = publish
        self._prefix = topic_prefix
        self._discovery = discovery_prefix
        self._announced: dict[str, str] = {}  # vehicle_id -> identity fingerprint

    def _device_block(self, vehicle: dict[str, Any]) -> dict[str, Any]:
        vid = vehicle["vehicle_id"]
        name = vehicle.get("name") or vid
        plate = vehicle.get("plate")
        block: dict[str, Any] = {
            "identifiers": [f"cartracker_{vid}"],
            "name": f"{name} ({plate})" if plate else name,
            "manufacturer": "JI ENGINEERING",
            "model": "car-tracker",
        }
        if vehicle.get("firmware"):
            block["sw_version"] = vehicle["firmware"]
        # The VIN is the identifier every system outside this project uses, so it
        # goes where HA shows a serial number rather than into an attribute.
        if vehicle.get("vin"):
            block["serial_number"] = vehicle["vin"]
        return block

    def _common(self, vehicle: dict[str, Any]) -> dict[str, Any]:
        vid = vehicle["vehicle_id"]
        return {
            "device": self._device_block(vehicle),
            "availability_topic": f"{self._prefix}/{vid}/status",
            "payload_available": "online",
            "payload_not_available": "offline",
        }

    def _config_topic(self, component: str, vid: str, key: str) -> str:
        return f"{self._discovery}/{component}/cartracker_{vid}/{key}/config"

    def fingerprint(self, vehicle: dict[str, Any]) -> str:
        """What has to change before the configs are worth republishing."""
        return "|".join(
            str(vehicle.get(k) or "")
            for k in ("name", "plate", "vin", "firmware")
        ) + f"|{LAYOUT_VERSION}"

    def announce(self, vehicle: dict[str, Any], force: bool = False) -> int:
        """Publish every config for one vehicle. Returns how many were sent."""
        vid = vehicle["vehicle_id"]
        fp = self.fingerprint(vehicle)
        if not force and self._announced.get(vid) == fp:
            return 0

        common = self._common(vehicle)
        base = f"{self._prefix}/{vid}"
        sent = 0

        for spec in DIRECT + DIRECT_BINARY + DERIVED:
            cfg = {
                **common,
                **spec["config"],
                "state_topic": f"{base}/{spec['source']}",
                "unique_id": f"cartracker_{vid}_{spec['key']}",
                "object_id": f"{vid}_{spec['key']}",
            }
            self._publish(self._config_topic(spec["component"], vid, spec["key"]),
                          json.dumps(cfg, ensure_ascii=False), retain=True)
            sent += 1

        # No availability topic on the tracker, unlike every other entity. An
        # offline tracker is exactly when the last known position matters most,
        # and availability would replace it with "unavailable" on the map. The
        # diagnostic entities still go unavailable, so the offline state is
        # visible without hiding the car.
        tracker = {k: v for k, v in common.items() if not k.startswith(
            ("availability", "payload_"))}
        tracker |= {
            "name": None,  # the device name becomes the entity name
            "unique_id": f"cartracker_{vid}_tracker",
            "object_id": vid,
            "json_attributes_topic": f"{base}/pos",
            "json_attributes_template": (  # noqa: UP031 - Jinja braces make str.format unreadable
                '{"latitude": {{ value_json.lat }},'
                ' "longitude": {{ value_json.lon }},'
                ' "gps_accuracy": {{ (value_json.hdop | float(2)) * %s }},'
                ' "course": {{ value_json.crs | default(0) }},'
                ' "satellites": {{ value_json.sat | default(0) }}}'
            ) % GNSS_BASE_M,
            "source_type": "gps",
        }
        self._publish(self._config_topic("device_tracker", vid, "tracker"),
                      json.dumps(tracker, ensure_ascii=False), retain=True)
        sent += 1

        for button in BUTTONS:
            cfg = {
                **common,
                "name": button["name"],
                "icon": button["icon"],
                "unique_id": f"cartracker_{vid}_{button['key']}",
                "object_id": f"{vid}_{button['key']}",
                "command_topic": f"{base}/cmd",
                "payload_press": json.dumps({"id": "ha", "cmd": button["key"]}),
                "retain": False,
            }
            self._publish(self._config_topic("button", vid, button["key"]),
                          json.dumps(cfg, ensure_ascii=False), retain=True)
            sent += 1

        self._announced[vid] = fp
        _LOG.info("discovery: %s announced, %d entities", vid, sent)
        return sent

    def publish_trip(self, vehicle_id: str, trip: dict[str, Any] | None,
                     now: float) -> None:
        """State for the derived sensors.

        Retained, because a trip that ended yesterday is still the answer to
        "how far did it go last time" after HA restarts.
        """
        if trip is None:
            payload = {"distance_km": 0, "duration_s": 0, "max_speed": 0,
                       "avg_speed": 0, "open": False}
        else:
            ended = trip.get("ended")
            payload = {
                "distance_km": round(trip.get("distance_km") or 0.0, 2),
                "duration_s": int((ended or now) - trip["started"]),
                "max_speed": round(trip.get("max_speed") or 0.0, 1),
                "avg_speed": round(trip.get("avg_speed") or 0.0, 1),
                "open": bool(trip.get("open")),
            }
        self._publish(f"{self._prefix}/{vehicle_id}/trip",
                      json.dumps(payload), retain=True)
