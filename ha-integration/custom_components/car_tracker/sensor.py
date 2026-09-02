"""Sensors: battery voltage, link quality, trip statistics, backlog."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

from homeassistant.components.sensor import (
    SensorDeviceClass,
    SensorEntity,
    SensorEntityDescription,
    SensorStateClass,
)
from homeassistant.const import (
    EntityCategory,
    UnitOfElectricPotential,
    UnitOfLength,
    UnitOfSpeed,
    UnitOfTime,
)
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from . import CarTrackerEntry
from .const import CONF_NAME
from .coordinator import TrackerData
from .entity import CarTrackerEntity


@dataclass(frozen=True, kw_only=True)
class CarTrackerSensorDescription(SensorEntityDescription):
    value_fn: Callable[[TrackerData], Any]


SENSORS: tuple[CarTrackerSensorDescription, ...] = (
    CarTrackerSensorDescription(
        key="voltage",
        translation_key="voltage",
        device_class=SensorDeviceClass.VOLTAGE,
        state_class=SensorStateClass.MEASUREMENT,
        native_unit_of_measurement=UnitOfElectricPotential.VOLT,
        suggested_display_precision=2,
        value_fn=lambda d: d.voltage,
    ),
    CarTrackerSensorDescription(
        key="speed",
        translation_key="speed",
        device_class=SensorDeviceClass.SPEED,
        state_class=SensorStateClass.MEASUREMENT,
        native_unit_of_measurement=UnitOfSpeed.KILOMETERS_PER_HOUR,
        suggested_display_precision=0,
        value_fn=lambda d: d.speed,
    ),
    CarTrackerSensorDescription(
        key="satellites",
        translation_key="satellites",
        state_class=SensorStateClass.MEASUREMENT,
        entity_category=EntityCategory.DIAGNOSTIC,
        value_fn=lambda d: d.satellites,
    ),
    CarTrackerSensorDescription(
        key="rssi",
        translation_key="rssi",
        device_class=SensorDeviceClass.SIGNAL_STRENGTH,
        state_class=SensorStateClass.MEASUREMENT,
        native_unit_of_measurement="dBm",
        entity_category=EntityCategory.DIAGNOSTIC,
        value_fn=lambda d: d.rssi,
    ),
    CarTrackerSensorDescription(
        key="network",
        translation_key="network",
        entity_category=EntityCategory.DIAGNOSTIC,
        value_fn=lambda d: d.network,
    ),
    CarTrackerSensorDescription(
        key="mode",
        translation_key="mode",
        device_class=SensorDeviceClass.ENUM,
        options=["parked", "driving", "moved", "hibernate"],
        value_fn=lambda d: d.mode,
    ),
    # The backlog depth is the earliest warning that the link is degrading,
    # long before the tracker goes quiet (docs/05 section 5.3).
    CarTrackerSensorDescription(
        key="queued",
        translation_key="queued",
        state_class=SensorStateClass.MEASUREMENT,
        entity_category=EntityCategory.DIAGNOSTIC,
        value_fn=lambda d: d.queued,
    ),
    CarTrackerSensorDescription(
        key="uptime",
        translation_key="uptime",
        device_class=SensorDeviceClass.DURATION,
        native_unit_of_measurement=UnitOfTime.SECONDS,
        entity_category=EntityCategory.DIAGNOSTIC,
        value_fn=lambda d: d.uptime,
    ),
    CarTrackerSensorDescription(
        key="trip_distance",
        translation_key="trip_distance",
        device_class=SensorDeviceClass.DISTANCE,
        state_class=SensorStateClass.MEASUREMENT,
        native_unit_of_measurement=UnitOfLength.KILOMETERS,
        suggested_display_precision=1,
        value_fn=lambda d: round(d.trip.distance_km, 2) if d.trip else None,
    ),
    CarTrackerSensorDescription(
        key="last_trip_distance",
        translation_key="last_trip_distance",
        device_class=SensorDeviceClass.DISTANCE,
        state_class=SensorStateClass.MEASUREMENT,
        native_unit_of_measurement=UnitOfLength.KILOMETERS,
        suggested_display_precision=1,
        value_fn=lambda d: round(d.last_trip.distance_km, 2) if d.last_trip else None,
    ),
    CarTrackerSensorDescription(
        key="last_trip_duration",
        translation_key="last_trip_duration",
        device_class=SensorDeviceClass.DURATION,
        native_unit_of_measurement=UnitOfTime.MINUTES,
        suggested_display_precision=0,
        value_fn=lambda d: round(d.last_trip.duration_s / 60, 1) if d.last_trip else None,
    ),
    CarTrackerSensorDescription(
        key="last_trip_max_speed",
        translation_key="last_trip_max_speed",
        device_class=SensorDeviceClass.SPEED,
        native_unit_of_measurement=UnitOfSpeed.KILOMETERS_PER_HOUR,
        suggested_display_precision=0,
        value_fn=lambda d: round(d.last_trip.max_speed) if d.last_trip else None,
    ),
)


async def async_setup_entry(
    hass: HomeAssistant, entry: CarTrackerEntry, async_add_entities: AddEntitiesCallback
) -> None:
    name = entry.data[CONF_NAME]
    async_add_entities(
        CarTrackerSensor(entry.runtime_data, name, description)
        for description in SENSORS
    )


class CarTrackerSensor(CarTrackerEntity, SensorEntity):
    entity_description: CarTrackerSensorDescription

    def __init__(self, coordinator, name: str, description) -> None:
        super().__init__(coordinator, name, description.key)
        self.entity_description = description

    @property
    def native_value(self) -> Any:
        return self.entity_description.value_fn(self.coordinator.data)
