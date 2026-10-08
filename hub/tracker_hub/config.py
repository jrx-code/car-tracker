"""Configuration, read from the environment.

Secrets come from /etc/tracker-hub/tracker-hub.env, which systemd loads and
which is populated from the password manager at deploy time. Nothing secret lives here.
"""

from __future__ import annotations

import os
from dataclasses import dataclass, field


def _env(name: str, default: str = "") -> str:
    return os.environ.get(name, default).strip()


def _env_int(name: str, default: int) -> int:
    raw = _env(name)
    return int(raw) if raw else default


def _env_float(name: str, default: float) -> float:
    raw = _env(name)
    return float(raw) if raw else default


def _env_list(name: str, default: str = "") -> tuple[str, ...]:
    raw = _env(name, default)
    return tuple(x.strip() for x in raw.split(",") if x.strip())


def _env_bool(name: str, default: bool) -> bool:
    raw = _env(name).lower()
    if not raw:
        return default
    return raw in ("1", "true", "yes", "on")


@dataclass(frozen=True)
class Config:
    # --- MQTT ---
    mqtt_host: str = field(default_factory=lambda: _env("MQTT_HOST", "localhost"))
    mqtt_port: int = field(default_factory=lambda: _env_int("MQTT_PORT", 8883))
    mqtt_user: str = field(default_factory=lambda: _env("MQTT_USER", "tracker-hub"))
    mqtt_pass: str = field(default_factory=lambda: _env("MQTT_PASS"))
    mqtt_tls: bool = field(default_factory=lambda: _env_bool("MQTT_TLS", True))
    mqtt_ca: str = field(default_factory=lambda: _env("MQTT_CA"))
    topic_prefix: str = field(default_factory=lambda: _env("TOPIC_PREFIX", "cartracker"))

    # --- Home Assistant discovery ---
    # The hub tells HA what entities exist, but most of them read the device's
    # own topics, so a stopped hub costs only the trip sensors.
    discovery: bool = field(default_factory=lambda: _env_bool("HA_DISCOVERY", True))
    discovery_prefix: str = field(
        default_factory=lambda: _env("HA_DISCOVERY_PREFIX", "homeassistant")
    )

    # --- HTTP ---
    http_host: str = field(default_factory=lambda: _env("HTTP_HOST", "0.0.0.0"))
    http_port: int = field(default_factory=lambda: _env_int("HTTP_PORT", 8080))

    # --- Admin mode ---
    # Identity comes from Authentik through gateway-caddy (forward_auth on
    # /admin, /api/admin and /device). The headers are trusted only when the
    # request arrives from one of these addresses: the hub also listens on the
    # LAN, and anything there could send an X-Authentik-Username of its own.
    admin_users: tuple[str, ...] = field(default_factory=lambda: _env_list("ADMIN_USERS"))
    trusted_proxies: tuple[str, ...] = field(
        default_factory=lambda: _env_list("TRUSTED_PROXIES")
    )
    # Password of the trackers' own portals, sent as X-Admin-Pass on behalf of
    # a signed-in admin so nobody has to type it. One value for the fleet.
    device_admin_pass: str = field(default_factory=lambda: _env("DEVICE_ADMIN_PASS"))

    # --- Storage ---
    db_path: str = field(
        default_factory=lambda: _env("DB_PATH", "/var/lib/tracker-hub/tracker.db")
    )
    # Raw positions are the sensitive part of this dataset, so they expire.
    # Trips stay: they carry the useful summary without the minute-by-minute
    # movement history. Same reasoning as docs/09 of the car-tracker repo.
    retain_positions_days: int = field(
        default_factory=lambda: _env_int("RETAIN_POSITIONS_DAYS", 180)
    )

    # --- Derived data ---
    # A position that would require this speed to be reached is a GNSS glitch.
    max_plausible_speed_kmh: float = field(
        default_factory=lambda: _env_float("MAX_PLAUSIBLE_SPEED_KMH", 300.0)
    )
    max_hdop: float = field(default_factory=lambda: _env_float("MAX_HDOP", 5.0))
    # A tracker that has said nothing for this long is shown as stale even when
    # the broker has not yet published its last will.
    stale_after_s: int = field(default_factory=lambda: _env_int("STALE_AFTER_S", 5400))

    def masked(self) -> dict[str, object]:
        """Config for logs and the /api/health endpoint, without secrets."""
        return {
            "mqtt": f"{self.mqtt_host}:{self.mqtt_port}",
            "mqtt_user": self.mqtt_user,
            "mqtt_tls": self.mqtt_tls,
            "topic_prefix": self.topic_prefix,
            "http": f"{self.http_host}:{self.http_port}",
            "db_path": self.db_path,
            "retain_positions_days": self.retain_positions_days,
            "ha_discovery": self.discovery,
            "admin_users": len(self.admin_users),
            "device_admin_pass_set": bool(self.device_admin_pass),
        }
