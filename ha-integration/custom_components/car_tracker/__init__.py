"""The car_tracker integration."""

from __future__ import annotations

import logging

import voluptuous as vol
from homeassistant.components import mqtt
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import Platform
from homeassistant.core import HomeAssistant, ServiceCall
from homeassistant.exceptions import ConfigEntryNotReady
from homeassistant.helpers import config_validation as cv

from .const import (
    CONF_TOPIC_PREFIX,
    CONF_VEHICLE_ID,
    DEFAULT_TOPIC_PREFIX,
    DOMAIN,
    SERVICE_LOCATE,
    SERVICE_PING,
    SERVICE_SET_CONFIG,
)
from .coordinator import CarTrackerCoordinator

_LOGGER = logging.getLogger(__name__)

PLATFORMS: list[Platform] = [
    Platform.BINARY_SENSOR,
    Platform.DEVICE_TRACKER,
    Platform.SENSOR,
]

type CarTrackerEntry = ConfigEntry[CarTrackerCoordinator]

_SERVICE_SCHEMA = vol.Schema({vol.Required("entry_id"): cv.string})

_SET_CONFIG_SCHEMA = _SERVICE_SCHEMA.extend(
    {vol.Required("config"): vol.Schema({cv.string: vol.Any(int, float, str, bool)})}
)


async def async_setup_entry(hass: HomeAssistant, entry: CarTrackerEntry) -> bool:
    """Set up one vehicle from a config entry."""
    if not await mqtt.async_wait_for_mqtt_client(hass):
        raise ConfigEntryNotReady("MQTT integration is not available")

    coordinator = CarTrackerCoordinator(
        hass,
        entry.entry_id,
        entry.data.get(CONF_TOPIC_PREFIX, DEFAULT_TOPIC_PREFIX),
        entry.data[CONF_VEHICLE_ID],
    )
    await coordinator.async_setup()
    entry.runtime_data = coordinator

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    entry.async_on_unload(entry.add_update_listener(_async_update_listener))
    _async_register_services(hass)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: CarTrackerEntry) -> bool:
    """Unload a config entry."""
    unloaded = await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
    if unloaded:
        await entry.runtime_data.async_unload()
    return unloaded


async def _async_update_listener(hass: HomeAssistant, entry: CarTrackerEntry) -> None:
    """Push changed options down to the device as a retained cfg."""
    if entry.options:
        await entry.runtime_data.async_publish_config(dict(entry.options))
    await hass.config_entries.async_reload(entry.entry_id)


def _async_register_services(hass: HomeAssistant) -> None:
    if hass.services.has_service(DOMAIN, SERVICE_LOCATE):
        return

    def _coordinator(call: ServiceCall) -> CarTrackerCoordinator:
        entry = hass.config_entries.async_get_entry(call.data["entry_id"])
        if entry is None or entry.domain != DOMAIN:
            raise vol.Invalid(f"unknown car_tracker entry {call.data['entry_id']}")
        return entry.runtime_data

    async def handle_locate(call: ServiceCall) -> None:
        await _coordinator(call).async_send_command("locate")

    async def handle_ping(call: ServiceCall) -> None:
        await _coordinator(call).async_send_command("ping")

    async def handle_set_config(call: ServiceCall) -> None:
        await _coordinator(call).async_publish_config(call.data["config"])

    hass.services.async_register(DOMAIN, SERVICE_LOCATE, handle_locate, _SERVICE_SCHEMA)
    hass.services.async_register(DOMAIN, SERVICE_PING, handle_ping, _SERVICE_SCHEMA)
    hass.services.async_register(
        DOMAIN, SERVICE_SET_CONFIG, handle_set_config, _SET_CONFIG_SCHEMA
    )
