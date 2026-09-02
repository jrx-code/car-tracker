"""Constants for the car_tracker integration.

The wire format is defined in docs/05-protokol-mqtt.md of the car-tracker repo.
Change both sides together.
"""

from __future__ import annotations

from typing import Final

DOMAIN: Final = "car_tracker"

CONF_VEHICLE_ID: Final = "vehicle_id"
CONF_NAME: Final = "name"
CONF_TOPIC_PREFIX: Final = "topic_prefix"

DEFAULT_TOPIC_PREFIX: Final = "cartracker"

# Sub-topics, relative to <prefix>/<vehicle_id>/
TOPIC_STATUS: Final = "status"
TOPIC_INFO: Final = "info"
TOPIC_POS: Final = "pos"
TOPIC_TEL: Final = "tel"
TOPIC_EVT: Final = "evt"
TOPIC_BATCH: Final = "batch"
TOPIC_CFG: Final = "cfg"
TOPIC_CMD: Final = "cmd"
TOPIC_ACK: Final = "ack"

# Device modes reported in the "st" field.
MODE_PARKED: Final = "parked"
MODE_DRIVING: Final = "driving"
MODE_MOVED: Final = "moved"
MODE_HIBERNATE: Final = "hibernate"

# A position further than this from the previous one, in a time window that
# makes it physically impossible, is treated as a GNSS glitch and dropped.
MAX_PLAUSIBLE_SPEED_KMH: Final = 300.0

# Positions with a worse HDOP never reach the map. The firmware filters too,
# but a filter on the receiving side survives an old firmware in the field.
MAX_HDOP: Final = 5.0

SIGNAL_POSITION: Final = f"{DOMAIN}_position"
SIGNAL_TELEMETRY: Final = f"{DOMAIN}_telemetry"
SIGNAL_EVENT: Final = f"{DOMAIN}_event"

EVENT_TRIP_START: Final = "trip_start"
EVENT_TRIP_END: Final = "trip_end"
EVENT_MOTION_ALARM: Final = "motion_alarm"

SERVICE_LOCATE: Final = "locate"
SERVICE_PING: Final = "ping"
SERVICE_SET_CONFIG: Final = "set_config"
