"""Binary sensors: driving, motion alarm, low battery, roaming."""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass

from homeassistant.components.binary_sensor import (
    BinarySensorDeviceClass,
    BinarySensorEntity,
    BinarySensorEntityDescription,
)
from homeassistant.const import EntityCategory
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from . import CarTrackerEntry
from .const import CONF_NAME, MODE_DRIVING, MODE_HIBERNATE, MODE_MOVED
from .coordinator import TrackerData
from .entity import CarTrackerEntity

# Below this the car battery is low enough to be worth a notification. Kept in
# sync with v_warn in the firmware config (docs/04 section 4.5).
LOW_VOLTAGE = 12.2


@dataclass(frozen=True, kw_only=True)
class CarTrackerBinaryDescription(BinarySensorEntityDescription):
    value_fn: Callable[[TrackerData], bool | None]


BINARY_SENSORS: tuple[CarTrackerBinaryDescription, ...] = (
    CarTrackerBinaryDescription(
        key="driving",
        translation_key="driving",
        device_class=BinarySensorDeviceClass.MOVING,
        value_fn=lambda d: d.mode == MODE_DRIVING,
    ),
    # Movement with the engine off: towing or theft. The device decides this on
    # its own so it also works with HA down (docs/02 section 2.8).
    CarTrackerBinaryDescription(
        key="motion_alarm",
        translation_key="motion_alarm",
        device_class=BinarySensorDeviceClass.PROBLEM,
        value_fn=lambda d: d.mode == MODE_MOVED,
    ),
    CarTrackerBinaryDescription(
        key="battery_low",
        translation_key="battery_low",
        device_class=BinarySensorDeviceClass.BATTERY,
        value_fn=lambda d: None if d.voltage is None else d.voltage < LOW_VOLTAGE,
    ),
    CarTrackerBinaryDescription(
        key="hibernate",
        translation_key="hibernate",
        entity_category=EntityCategory.DIAGNOSTIC,
        value_fn=lambda d: d.mode == MODE_HIBERNATE,
    ),
    CarTrackerBinaryDescription(
        key="roaming",
        translation_key="roaming",
        entity_category=EntityCategory.DIAGNOSTIC,
        value_fn=lambda d: d.roaming,
    ),
)


async def async_setup_entry(
    hass: HomeAssistant, entry: CarTrackerEntry, async_add_entities: AddEntitiesCallback
) -> None:
    name = entry.data[CONF_NAME]
    async_add_entities(
        CarTrackerBinarySensor(entry.runtime_data, name, description)
        for description in BINARY_SENSORS
    )


class CarTrackerBinarySensor(CarTrackerEntity, BinarySensorEntity):
    entity_description: CarTrackerBinaryDescription

    def __init__(self, coordinator, name: str, description) -> None:
        super().__init__(coordinator, name, description.key)
        self.entity_description = description

    @property
    def is_on(self) -> bool | None:
        return self.entity_description.value_fn(self.coordinator.data)
