"""HTTP layer: JSON API plus the static UI.

Standard library only. This service has one job and a handful of endpoints, so
a framework would be more dependency than help.
"""

from __future__ import annotations

import json
import logging
import mimetypes
import os
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.parse import parse_qs, urlparse
from urllib.request import Request, urlopen

from .config import Config
from .ingest import Ingest
from .ota import DEFAULT_CHUNK, MAX_IMAGE, OtaError
from .store import Store

_LOG = logging.getLogger(__name__)

WEB_ROOT = Path(__file__).resolve().parent.parent / "web"

# Writing commands to a vehicle is a different risk class from reading its
# position, so it can be gated by a token even when the page itself is open.
API_TOKEN = os.environ.get("API_TOKEN", "").strip()


def _json_bytes(data: Any) -> bytes:
    return json.dumps(data, default=str).encode("utf-8")


class Handler(BaseHTTPRequestHandler):
    server_version = "tracker-hub"
    protocol_version = "HTTP/1.1"

    cfg: Config
    store: Store
    ingest: Ingest
    started_at: float

    # --- helpers ----------------------------------------------------------

    def _send_json(self, data: Any, status: int = 200) -> None:
        body = json.dumps(data, default=str).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _send_error_json(self, status: int, message: str) -> None:
        self._send_json({"error": message}, status)

    def _send_file(self, path: Path) -> None:
        if not path.is_file():
            self._send_error_json(404, "not found")
            return
        body = path.read_bytes()
        ctype = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt: str, *args: Any) -> None:
        _LOG.debug("%s - %s", self.address_string(), fmt % args)

    def _query(self) -> dict[str, list[str]]:
        return parse_qs(urlparse(self.path).query)

    def _q_float(self, key: str, default: float | None = None) -> float | None:
        raw = self._query().get(key, [None])[0]
        if raw is None:
            return default
        try:
            return float(raw)
        except ValueError:
            return default

    def _admin(self) -> str | None:
        """The signed-in admin, or None.

        Authentik puts the user in X-Authentik-Username at gateway-caddy. The
        header only counts when the request comes from that proxy and names a
        user on ADMIN_USERS: the hub also listens on the LAN, where anyone can
        send the header themselves.
        """
        if self.client_address[0] not in self.cfg.trusted_proxies:
            return None
        user = self.headers.get("X-Authentik-Username", "").strip()
        if user and user in self.cfg.admin_users:
            return user
        return None

    def _authorised(self) -> bool:
        """Writes to a vehicle: a signed-in admin, or the API token if one is set.

        Before admin mode an empty API_TOKEN meant "open"; it no longer does.
        """
        if self._admin():
            return True
        return bool(API_TOKEN) and self.headers.get("X-Api-Token", "") == API_TOKEN

    def _read_body(self) -> bytes:
        length = int(self.headers.get("Content-Length") or 0)
        return self.rfile.read(length) if length else b""

    # --- routing ----------------------------------------------------------

    # --- device portal proxy ---------------------------------------------

    def _device_call(self, vehicle_id: str, rest: str, method: str,
                     body: bytes | None,
                     content_type: str | None = None) -> tuple[int, bytes, str]:
        """One request to a device portal: (status, payload, content type)."""
        vehicle = self.store.vehicle(vehicle_id)
        if vehicle is None:
            return 404, _json_bytes({"error": f"unknown vehicle {vehicle_id}"}), \
                "application/json"
        ip = vehicle.get("ip")
        if not ip:
            return 503, _json_bytes({
                "error": "the device has not reported an address yet; it does so "
                         "with every telemetry packet, so wait for the next one",
            }), "application/json"

        query = urlparse(self.path).query
        url = f"http://{ip}/{rest}" + (f"?{query}" if query else "")
        req = Request(url, data=body, method=method)
        ctype = content_type or self.headers.get("Content-Type")
        if ctype:
            req.add_header("Content-Type", ctype)
        # The portal's own admin password: whatever the page typed, or, for a
        # signed-in admin who typed nothing, the fleet password from the env.
        typed = self.headers.get("X-Admin-Pass", "")
        password = typed or self.cfg.device_admin_pass
        if password:
            req.add_header("X-Admin-Pass", password)

        try:
            with urlopen(req, timeout=8) as resp:
                return resp.status, resp.read(), \
                    resp.headers.get("Content-Type", "application/octet-stream")
        except HTTPError as exc:
            return exc.code, exc.read(), "application/json"
        except (URLError, TimeoutError, OSError) as exc:
            return 504, _json_bytes({"error": f"device at {ip} did not answer: {exc}"}), \
                "application/json"

    # Each tracker serves its own configuration portal, but it lives on an
    # address handed out by DHCP, on the IoT VLAN, over plain HTTP. Proxying it
    # here means one hostname, one TLS certificate and one place to reason about
    # access. Admin only: the portal can change WiFi, MQTT and SIM settings.
    #
    # It only works while the hub can route to the device: on the home network,
    # or over the modem's LAN. A tracker on a mobile network has a private
    # carrier address and is not reachable this way; that case is what the MQTT
    # command channel and the device's own emergency AP are for.
    def _proxy_device(self, vehicle_id: str, rest: str, method: str,
                      body: bytes | None) -> None:
        status, payload, ctype = self._device_call(vehicle_id, rest, method, body)
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self) -> None:
        path = urlparse(self.path).path.rstrip("/") or "/"

        if path.startswith("/device/"):
            if not self._admin():
                self._send_error_json(403, "admin mode required")
                return
            parts = path.split("/", 3)
            self._proxy_device(parts[2], parts[3] if len(parts) > 3 else "", "GET", None)
            return

        # Reached only after Authentik let the browser through; the page then
        # knows it may ask /api/admin for the rest.
        if path == "/admin":
            if not self._admin():
                self._send_error_json(403, "this account is not a tracker admin")
                return
            self.send_response(302)
            self.send_header("Location", "/#admin")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        if path.startswith("/api/admin"):
            self._admin_get(path)
            return

        if not path.startswith("/api"):
            self._serve_static(path)
            return

        parts = [p for p in path.split("/") if p]  # ['api', ...]

        try:
            if path == "/api/health":
                self._send_json(self._health())
            elif path == "/api/vehicles":
                self._send_json({"vehicles": self._vehicles()})
            elif len(parts) == 3 and parts[1] == "vehicles":
                self._vehicle_detail(parts[2])
            elif len(parts) == 4 and parts[1] == "vehicles":
                self._vehicle_subresource(parts[2], parts[3])
            elif len(parts) == 3 and parts[1] == "trips":
                self._trip_detail(parts[2])
            elif path == "/api/trips":
                limit = int(self._q_float("limit", 50) or 50)
                self._send_json({"trips": self.store.trips(limit=limit)})
            elif path == "/api/events":
                limit = int(self._q_float("limit", 100) or 100)
                self._send_json({"events": self.store.events(limit=limit)})
            else:
                self._send_error_json(404, "unknown endpoint")
        except Exception as exc:  # noqa: BLE001 - never take the server down
            _LOG.exception("GET %s failed", self.path)
            self._send_error_json(500, str(exc))

    def do_POST(self) -> None:
        path = urlparse(self.path).path.rstrip("/")
        parts = [p for p in path.split("/") if p]

        if path.startswith("/device/"):
            raw = self._read_body()
            if not self._admin():
                self._send_error_json(403, "admin mode required")
                return
            segs = path.split("/", 3)
            self._proxy_device(segs[2], segs[3] if len(segs) > 3 else "", "POST", raw)
            return

        if not self._authorised():
            self._send_error_json(403, "admin mode or X-Api-Token required")
            return

        # Firmware upload: a raw image body, not JSON, so it is routed first.
        if len(parts) == 4 and parts[1] == "vehicles" and parts[3] == "ota":
            self._post_ota(parts[2])
            return

        raw = self._read_body() or b"{}"
        try:
            body = json.loads(raw or b"{}")
        except ValueError:
            self._send_error_json(400, "body is not JSON")
            return

        try:
            if len(parts) == 5 and parts[1] == "admin" and parts[2] == "vehicles":
                self._admin_post(parts[3], parts[4], body)
            elif len(parts) == 4 and parts[1] == "vehicles" and parts[3] == "command":
                self._post_command(parts[2], body)
            elif len(parts) == 4 and parts[1] == "vehicles" and parts[3] == "config":
                self._post_config(parts[2], body)
            else:
                self._send_error_json(404, "unknown endpoint")
        except Exception as exc:  # noqa: BLE001
            _LOG.exception("POST %s failed", self.path)
            self._send_error_json(500, str(exc))

    # --- endpoints --------------------------------------------------------

    def _health(self) -> dict[str, Any]:
        return {
            "ok": self.ingest.connected,
            "uptime_s": int(time.time() - self.started_at),
            "mqtt": self.ingest.health(),
            "db": self.store.stats(),
            "config": self.cfg.masked(),
        }

    def _vehicles(self) -> list[dict[str, Any]]:
        """Every known vehicle with its last position folded in."""
        out = []
        now = time.time()
        for vehicle in self.store.vehicles():
            vid = vehicle["vehicle_id"]
            last = self.store.last_position(vid)
            trip = self.store.open_trip(vid)
            last_seen = vehicle.get("last_seen") or 0
            vehicle["stale"] = bool(
                last_seen and (now - last_seen) > self.cfg.stale_after_s
            )
            vehicle["age_s"] = int(now - last_seen) if last_seen else None
            vehicle["position"] = last
            vehicle["open_trip"] = trip
            # Only advertise the portal when there is an address to proxy to.
            vehicle["portal"] = f"/device/{vid}/" if vehicle.get("ip") else None
            out.append(vehicle)
        return out

    def _vehicle_detail(self, vehicle_id: str) -> None:
        vehicle = self.store.vehicle(vehicle_id)
        if vehicle is None:
            self._send_error_json(404, f"unknown vehicle {vehicle_id}")
            return
        vehicle["position"] = self.store.last_position(vehicle_id)
        vehicle["open_trip"] = self.store.open_trip(vehicle_id)
        vehicle["trips"] = self.store.trips(vehicle_id, limit=20)
        vehicle["events"] = self.store.events(vehicle_id, limit=20)
        self._send_json(vehicle)

    def _vehicle_subresource(self, vehicle_id: str, resource: str) -> None:
        if resource == "positions":
            trip_raw = self._query().get("trip", [None])[0]
            trip_id = int(trip_raw) if trip_raw and trip_raw.isdigit() else None
            hours = self._q_float("hours", 24.0) or 24.0
            since = self._q_float("since") or (time.time() - hours * 3600)
            limit = int(self._q_float("limit", 5000) or 5000)
            self._send_json({
                "positions": self.store.positions(
                    vehicle_id, since=None if trip_id else since,
                    trip_id=trip_id, limit=limit,
                )
            })
        elif resource == "trips":
            limit = int(self._q_float("limit", 50) or 50)
            self._send_json({"trips": self.store.trips(vehicle_id, limit=limit)})
        elif resource == "events":
            limit = int(self._q_float("limit", 100) or 100)
            self._send_json({"events": self.store.events(vehicle_id, limit=limit)})
        elif resource == "telemetry":
            hours = self._q_float("hours", 168.0) or 168.0
            self._send_json({
                "telemetry": self.store.telemetry(vehicle_id, time.time() - hours * 3600)
            })
        else:
            self._send_error_json(404, f"unknown resource {resource}")

    def _trip_detail(self, trip_raw: str) -> None:
        if not trip_raw.isdigit():
            self._send_error_json(400, "trip id must be a number")
            return
        trip = self.store.trip(int(trip_raw))
        if trip is None:
            self._send_error_json(404, "unknown trip")
            return
        trip["positions"] = self.store.positions(
            trip["vehicle_id"], trip_id=trip["id"], limit=20000
        )
        self._send_json(trip)

    def _post_command(self, vehicle_id: str, body: dict[str, Any]) -> None:
        command = str(body.get("cmd", "")).strip()
        allowed = {"ping", "locate", "reboot"}
        if command not in allowed:
            self._send_error_json(400, f"cmd must be one of {sorted(allowed)}")
            return
        if self.store.vehicle(vehicle_id) is None:
            self._send_error_json(404, f"unknown vehicle {vehicle_id}")
            return
        ok = self.ingest.publish_command(vehicle_id, command)
        _LOG.info("command %s -> %s: %s", command, vehicle_id, "sent" if ok else "failed")
        self._send_json({"sent": ok, "cmd": command, "vehicle_id": vehicle_id})

    def _post_ota(self, vehicle_id: str) -> None:
        """Stage a signed image and tell the vehicle to fetch it over MQTT.

        Body: the raw firmware.bin. Header X-Firmware-Signature: base64 DER
        ECDSA P-256 over its SHA-256 (firmware/scripts/ota_release.sh).
        """
        length = int(self.headers.get("Content-Length") or 0)
        if not 0 < length <= MAX_IMAGE:
            self._send_error_json(413, f"image must be 1..{MAX_IMAGE} bytes")
            return
        image = self.rfile.read(length)
        if self.store.vehicle(vehicle_id) is None:
            self._send_error_json(404, f"unknown vehicle {vehicle_id}")
            return
        try:
            chunk = int(self._query().get("chunk", [DEFAULT_CHUNK])[0])
            result = self.ingest.ota.stage(
                vehicle_id, image, self.headers.get("X-Firmware-Signature", ""), chunk)
        except (OtaError, ValueError) as exc:
            self._send_error_json(400, str(exc))
            return
        _LOG.info("ota %s by %s: %d bytes", vehicle_id, self._admin() or "api token", length)
        self._send_json(result)

    def _post_config(self, vehicle_id: str, body: dict[str, Any]) -> None:
        if not isinstance(body, dict) or not body:
            self._send_error_json(400, "config must be a non-empty object")
            return
        # Same guard rail as the firmware: a config from the network must not be
        # able to let the tracker flatten the car battery.
        if "v_hib" in body and float(body["v_hib"]) < 11.0:
            self._send_error_json(400, "v_hib below 11.0 V is refused")
            return
        # cfg is retained and replaces what the broker holds, so a change to one
        # value is merged onto the last known set instead of dropping the rest.
        merged = {**self.ingest.last_cfg.get(vehicle_id, {}), **body}
        ok = self.ingest.publish_config(vehicle_id, merged)
        if ok:
            self.ingest.last_cfg[vehicle_id] = merged
        self._send_json({"sent": ok, "vehicle_id": vehicle_id, "config": merged})

    # --- admin -------------------------------------------------------------

    def _admin_get(self, path: str) -> None:
        user = self._admin()
        if not user:
            self._send_error_json(403, "admin mode required")
            return
        parts = [p for p in path.split("/") if p]  # ['api', 'admin', ...]
        if parts == ["api", "admin", "whoami"]:
            self._send_json({"user": user})
        elif len(parts) == 4 and parts[2] == "vehicles":
            self._admin_vehicle(parts[3])
        else:
            self._send_error_json(404, "unknown endpoint")

    def _admin_vehicle(self, vehicle_id: str) -> None:
        """Everything the settings tab shows, in one call.

        The device settings come live from the portal and can fail (a car on
        LTE is not reachable); the MQTT side (cfg, acks, info) still works.
        """
        vehicle = self.store.vehicle(vehicle_id)
        if vehicle is None:
            self._send_error_json(404, f"unknown vehicle {vehicle_id}")
            return
        status, payload, _ = self._device_call(vehicle_id, "api/settings", "GET", None)
        settings: dict[str, Any] | None = None
        error = None
        if status == 200:
            try:
                settings = json.loads(payload)
            except ValueError:
                error = "the device answered with something that is not JSON"
        else:
            try:
                error = json.loads(payload).get("error") or f"HTTP {status}"
            except ValueError:
                error = f"HTTP {status}"
        self._send_json({
            "vehicle": vehicle,
            "info": self.ingest.last_info.get(vehicle_id),
            "cfg": self.ingest.last_cfg.get(vehicle_id),
            "ota": self.ingest.ota.status(vehicle_id),
            "acks": list(self.ingest.acks.get(vehicle_id, [])),
            "settings": settings,
            "settings_error": error,
            "device_pass_configured": bool(self.cfg.device_admin_pass),
        })

    def _admin_post(self, vehicle_id: str, what: str, body: dict[str, Any]) -> None:
        if not self._admin():
            self._send_error_json(403, "admin mode required")
            return
        if what == "settings":
            self._forward_json(vehicle_id, "api/settings", body)
        elif what == "action":
            action = str(body.get("action", ""))
            allowed = {"reboot", "start_ap", "stop_ap"}
            if action not in allowed:
                self._send_error_json(400, f"action must be one of {sorted(allowed)}")
                return
            self._forward_json(vehicle_id, "api/action", {"action": action})
        elif what == "command":
            self._post_command(vehicle_id, body)
        elif what == "config":
            self._post_config(vehicle_id, body)
        else:
            self._send_error_json(404, "unknown endpoint")

    def _forward_json(self, vehicle_id: str, rest: str, body: dict[str, Any]) -> None:
        status, payload, ctype = self._device_call(
            vehicle_id, rest, "POST", _json_bytes(body), "application/json")
        _LOG.info("admin %s: %s %s -> %s", self._admin(), vehicle_id, rest, status)
        self.send_response(status)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(payload)

    # --- static -----------------------------------------------------------

    def _serve_static(self, path: str) -> None:
        rel = "index.html" if path == "/" else path.lstrip("/")
        target = (WEB_ROOT / rel).resolve()
        # Path traversal guard: everything served must live under WEB_ROOT.
        if not str(target).startswith(str(WEB_ROOT.resolve())):
            self._send_error_json(403, "forbidden")
            return
        self._send_file(target)


def serve(cfg: Config, store: Store, ingest: Ingest) -> ThreadingHTTPServer:
    Handler.cfg = cfg
    Handler.store = store
    Handler.ingest = ingest
    Handler.started_at = time.time()
    httpd = ThreadingHTTPServer((cfg.http_host, cfg.http_port), Handler)
    httpd.daemon_threads = True
    _LOG.info("HTTP listening on %s:%d", cfg.http_host, cfg.http_port)
    return httpd
