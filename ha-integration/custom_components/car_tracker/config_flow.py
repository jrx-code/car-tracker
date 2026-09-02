"""Config and options flow."""

from __future__ import annotations

from typing import Any

import voluptuous as vol
from homeassistant.components import mqtt
from homeassistant.config_entries import (
    ConfigEntry,
    ConfigFlow,
    ConfigFlowResult,
    OptionsFlow,
)
from homeassistant.core import callback
from homeassistant.helpers import config_validation as cv

from .const import (
    CONF_NAME,
    CONF_TOPIC_PREFIX,
    CONF_VEHICLE_ID,
    DEFAULT_TOPIC_PREFIX,
    DOMAIN,
)

STEP_USER_SCHEMA = vol.Schema(
    {
        vol.Required(CONF_VEHICLE_ID): cv.string,
        vol.Required(CONF_NAME): cv.string,
        vol.Optional(CONF_TOPIC_PREFIX, default=DEFAULT_TOPIC_PREFIX): cv.string,
    }
)

# Mirrors the cfg payload in docs/05 section 5.6. Editable here so thresholds can
# be corrected after the measurements on the car, without touching firmware.
OPTIONS_SCHEMA = vol.Schema(
    {
        vol.Optional("int_drive", default=30): vol.All(int, vol.Range(min=5, max=600)),
        vol.Optional("int_park", default=3600): vol.All(
            int, vol.Range(min=60, max=21600)
        ),
        vol.Optional("int_alarm", default=15): vol.All(int, vol.Range(min=5, max=300)),
        vol.Optional("v_drive_on", default=13.2): vol.All(
            vol.Coerce(float), vol.Range(min=12.5, max=15.0)
        ),
        vol.Optional("v_drive_off", default=13.0): vol.All(
            vol.Coerce(float), vol.Range(min=12.0, max=14.5)
        ),
        vol.Optional("v_warn", default=12.2): vol.All(
            vol.Coerce(float), vol.Range(min=11.0, max=13.0)
        ),
        # Never below 11.0 V: the tracker must not be allowed to flatten the car
        # battery past the point where the engine still starts (docs/01, Z2).
        vol.Optional("v_hib", default=11.9): vol.All(
            vol.Coerce(float), vol.Range(min=11.0, max=12.5)
        ),
        vol.Optional("v_wake", default=12.4): vol.All(
            vol.Coerce(float), vol.Range(min=11.5, max=13.5)
        ),
        vol.Optional("crs_delta", default=25): vol.All(int, vol.Range(min=5, max=90)),
        vol.Optional("hdop_max", default=3.0): vol.All(
            vol.Coerce(float), vol.Range(min=1.0, max=10.0)
        ),
        vol.Optional("motion_sens", default=3): vol.All(int, vol.Range(min=1, max=5)),
        vol.Optional("gnss_src", default="auto"): vol.In(["auto", "neo6m", "modem"]),
    }
)


class CarTrackerConfigFlow(ConfigFlow, domain=DOMAIN):
    """One config entry per vehicle."""

    VERSION = 1

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            if not await mqtt.async_wait_for_mqtt_client(self.hass):
                errors["base"] = "mqtt_unavailable"
            else:
                await self.async_set_unique_id(user_input[CONF_VEHICLE_ID])
                self._abort_if_unique_id_configured()
                return self.async_create_entry(
                    title=user_input[CONF_NAME], data=user_input
                )

        return self.async_show_form(
            step_id="user", data_schema=STEP_USER_SCHEMA, errors=errors
        )

    @staticmethod
    @callback
    def async_get_options_flow(entry: ConfigEntry) -> CarTrackerOptionsFlow:
        return CarTrackerOptionsFlow()


class CarTrackerOptionsFlow(OptionsFlow):
    """Editing options republishes the retained cfg topic."""

    async def async_step_init(
        self, user_input: dict[str, Any] | None = None
    ) -> ConfigFlowResult:
        if user_input is not None:
            return self.async_create_entry(data=user_input)

        return self.async_show_form(
            step_id="init",
            data_schema=self.add_suggested_values_to_schema(
                OPTIONS_SCHEMA, dict(self.config_entry.options)
            ),
        )
