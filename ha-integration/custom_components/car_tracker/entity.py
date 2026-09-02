"""Shared base entity."""

from __future__ import annotations

from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.dispatcher import async_dispatcher_connect
from homeassistant.helpers.entity import Entity

from .const import DOMAIN, SIGNAL_EVENT, SIGNAL_POSITION, SIGNAL_TELEMETRY
from .coordinator import CarTrackerCoordinator


class CarTrackerEntity(Entity):
    """Base for every car_tracker entity: one device per vehicle."""

    _attr_has_entity_name = True
    _attr_should_poll = False

    def __init__(self, coordinator: CarTrackerCoordinator, name: str, key: str) -> None:
        self.coordinator = coordinator
        self._attr_unique_id = f"{coordinator.vehicle_id}_{key}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, coordinator.vehicle_id)},
            name=name,
            manufacturer="JI ENGINEERING",
            model="car-tracker (ESP32 + GNSS + LTE)",
            sw_version=coordinator.data.firmware,
        )

    async def async_added_to_hass(self) -> None:
        for signal in (SIGNAL_POSITION, SIGNAL_TELEMETRY, SIGNAL_EVENT):
            self.async_on_remove(
                async_dispatcher_connect(
                    self.hass,
                    f"{signal}_{self.coordinator.entry_id}",
                    self._handle_update,
                )
            )

    def _handle_update(self) -> None:
        self.async_write_ha_state()

    @property
    def available(self) -> bool:
        # A device in hibernation is deliberately silent, not broken, so it stays
        # available and keeps reporting its last known battery voltage, which is
        # the whole point of that state (docs/02 section 2.3).
        return self.coordinator.data.online or self.coordinator.data.mode == "hibernate"
