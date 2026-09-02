"""Position on the map."""

from __future__ import annotations

from homeassistant.components.device_tracker import SourceType, TrackerEntity
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from . import CarTrackerEntry
from .const import CONF_NAME
from .entity import CarTrackerEntity


async def async_setup_entry(
    hass: HomeAssistant, entry: CarTrackerEntry, async_add_entities: AddEntitiesCallback
) -> None:
    async_add_entities([CarTrackerDeviceTracker(entry.runtime_data, entry.data[CONF_NAME])])


class CarTrackerDeviceTracker(CarTrackerEntity, TrackerEntity):
    """The vehicle as a device_tracker."""

    _attr_name = None  # takes the device name
    _attr_icon = "mdi:car-sports"

    def __init__(self, coordinator, name: str) -> None:
        super().__init__(coordinator, name, "tracker")

    @property
    def source_type(self) -> SourceType:
        return SourceType.GPS

    @property
    def latitude(self) -> float | None:
        return self.coordinator.data.lat

    @property
    def longitude(self) -> float | None:
        return self.coordinator.data.lon

    @property
    def location_accuracy(self) -> float:
        # HDOP is not metres. Multiplying by the nominal receiver accuracy gives
        # a usable radius: about 2.5 m for the GPS-only NEO-6M, better for the
        # multi-constellation receiver in the modem (docs/03 section 3.1).
        hdop = self.coordinator.data.hdop
        if hdop is None:
            return 0.0
        base = 1.5 if self.coordinator.data.gnss_source == "modem" else 2.5
        return round(hdop * base, 1)

    @property
    def extra_state_attributes(self) -> dict[str, object]:
        d = self.coordinator.data
        attrs: dict[str, object] = {
            "mode": d.mode,
            "speed_kmh": d.speed,
            "course": d.course,
            "satellites": d.satellites,
            "hdop": d.hdop,
            "gnss_source": d.gnss_source,
            "altitude": d.alt,
            "position_ts": d.position_ts,
            "position_ts_source": d.position_ts_source,
            "queued_points": d.queued,
        }
        if d.trip is not None:
            attrs["trip_distance_km"] = round(d.trip.distance_km, 2)
            attrs["trip_duration_s"] = int(d.trip.duration_s)
        return attrs
