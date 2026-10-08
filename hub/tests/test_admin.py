"""Admin mode: who may write, and what reaches the device.

Real HTTP on loopback: the hub under test and a stand-in for the tracker's own
portal. Loopback plays the part of gateway-caddy via TRUSTED_PROXIES.
"""

from __future__ import annotations

import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import ClassVar
from urllib.error import HTTPError
from urllib.request import HTTPRedirectHandler, Request, build_opener

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tracker_hub import web
from tracker_hub.config import Config
from tracker_hub.ingest import Ingest
from tracker_hub.store import Store

DEVICE_PASS = "fleet-secret"


class FakePortal(BaseHTTPRequestHandler):
    """The tracker's portal: remembers what it was sent."""

    seen: ClassVar[list[dict]] = []
    settings: ClassVar[dict] = {"apn": "internet", "sim_pin_set": False, "vehicle_id": "nd1"}

    def log_message(self, *args):
        pass

    def _reply(self, status, data):
        body = json.dumps(data).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        FakePortal.seen.append({"method": "GET", "path": self.path,
                                "pass": self.headers.get("X-Admin-Pass")})
        self._reply(200, FakePortal.settings)

    def do_POST(self):
        length = int(self.headers.get("Content-Length") or 0)
        body = json.loads(self.rfile.read(length) or b"{}")
        FakePortal.seen.append({"method": "POST", "path": self.path, "body": body,
                                "pass": self.headers.get("X-Admin-Pass")})
        if self.headers.get("X-Admin-Pass") != DEVICE_PASS:
            self._reply(401, {"error": "wrong or missing admin password"})
            return
        self._reply(200, {"ok": True})


def _start(server):
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


@pytest.fixture()
def env(tmp_path, monkeypatch):
    monkeypatch.setenv("MQTT_PASS", "test")
    monkeypatch.setenv("DB_PATH", str(tmp_path / "t.db"))
    monkeypatch.setenv("HTTP_HOST", "127.0.0.1")
    monkeypatch.setenv("HTTP_PORT", "0")
    monkeypatch.setenv("ADMIN_USERS", "admin,admin@example.com")
    monkeypatch.setenv("TRUSTED_PROXIES", "127.0.0.1")
    monkeypatch.setenv("DEVICE_ADMIN_PASS", DEVICE_PASS)
    monkeypatch.setattr(web, "API_TOKEN", "")

    portal = _start(ThreadingHTTPServer(("127.0.0.1", 0), FakePortal))
    FakePortal.seen = []

    cfg = Config()
    store = Store(cfg.db_path)
    ingest = Ingest(cfg, store)
    published = []
    monkeypatch.setattr(ingest, "publish_config",
                        lambda vid, c: published.append(("cfg", vid, c)) or True)
    monkeypatch.setattr(ingest, "publish_command",
                        lambda vid, c, **k: published.append(("cmd", vid, c)) or True)
    store.upsert_vehicle("nd1", ip=f"127.0.0.1:{portal.server_address[1]}",
                         last_seen=1.0)

    hub = _start(web.serve(cfg, store, ingest))
    base = f"http://127.0.0.1:{hub.server_address[1]}"
    yield base, ingest, published
    hub.shutdown()
    portal.shutdown()
    store.close()


class _NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


def call(base, path, method="GET", body=None, user=None, headers=None):
    req = Request(base + path, method=method,
                  data=json.dumps(body).encode() if body is not None else None)
    if body is not None:
        req.add_header("Content-Type", "application/json")
    if user:
        req.add_header("X-Authentik-Username", user)
    for k, v in (headers or {}).items():
        req.add_header(k, v)
    try:
        with build_opener(_NoRedirect).open(req, timeout=5) as resp:
            return resp.status, resp.headers, resp.read()
    except HTTPError as exc:
        return exc.code, exc.headers, exc.read()


def test_anonymous_cannot_use_admin_api(env):
    base, _, _ = env
    assert call(base, "/api/admin/whoami")[0] == 403
    assert call(base, "/device/nd1/api/settings")[0] == 403


def test_user_outside_admin_list_is_refused(env):
    base, _, _ = env
    assert call(base, "/api/admin/whoami", user="someone-else")[0] == 403


def test_header_from_untrusted_address_is_ignored(env, monkeypatch):
    base, _, _ = env
    # Config is frozen, so swap the whole object for one with another proxy.
    monkeypatch.setenv("TRUSTED_PROXIES", "192.0.2.10")
    monkeypatch.setattr(web.Handler, "cfg", Config())
    assert call(base, "/api/admin/whoami", user="admin")[0] == 403


def test_admin_whoami_and_redirect(env):
    base, _, _ = env
    status, _, body = call(base, "/api/admin/whoami", user="admin")
    assert status == 200 and json.loads(body) == {"user": "admin"}
    status, headers, _ = call(base, "/admin", user="admin")
    assert status == 302 and headers["Location"] == "/#admin"


def test_writes_are_closed_without_admin(env):
    """An empty API_TOKEN used to leave commands open to anyone on the LAN."""
    base, _, published = env
    assert call(base, "/api/vehicles/nd1/command", "POST", {"cmd": "reboot"})[0] == 403
    assert call(base, "/api/vehicles/nd1/config", "POST", {"int_park": 60})[0] == 403
    assert published == []


def test_settings_come_from_the_device_with_the_fleet_password(env):
    base, _, _ = env
    status, _, body = call(base, "/api/admin/vehicles/nd1", user="admin")
    data = json.loads(body)
    assert status == 200
    assert data["settings"]["apn"] == "internet"
    assert data["settings_error"] is None
    assert FakePortal.seen[-1]["pass"] == DEVICE_PASS


def test_settings_save_is_forwarded(env):
    base, _, _ = env
    status, _, body = call(base, "/api/admin/vehicles/nd1/settings", "POST",
                           {"apn": "internet.t-mobile.pl", "sim_pin": "1234"},
                           user="admin")
    assert status == 200 and json.loads(body) == {"ok": True}
    last = FakePortal.seen[-1]
    assert last["path"] == "/api/settings"
    assert last["body"] == {"apn": "internet.t-mobile.pl", "sim_pin": "1234"}
    assert last["pass"] == DEVICE_PASS


def test_unknown_device_action_is_refused(env):
    base, _, _ = env
    status, _, _ = call(base, "/api/admin/vehicles/nd1/action", "POST",
                        {"action": "factory_reset"}, user="admin")
    assert status == 400


def test_config_change_is_merged_onto_the_retained_set(env):
    base, ingest, published = env
    ingest.last_cfg["nd1"] = {"int_park": 3600, "v_hib": 11.8}
    status, _, _ = call(base, "/api/admin/vehicles/nd1/config", "POST",
                        {"int_drive": 10}, user="admin")
    assert status == 200
    assert published[-1] == ("cfg", "nd1",
                             {"int_park": 3600, "v_hib": 11.8, "int_drive": 10})


def test_ingest_keeps_cfg_ack_and_info(env):
    _, ingest, _ = env
    ingest._handle("cartracker/nd1/cfg", b'{"int_park": 900}')
    ingest._handle("cartracker/nd1/ack", b'{"id": "a1", "ok": true, "msg": ""}')
    ingest._handle("cartracker/nd1/info", b'{"fw": "0.1.0", "iccid": "8948"}')
    assert ingest.last_cfg["nd1"] == {"int_park": 900}
    assert ingest.acks["nd1"][0]["id"] == "a1"
    assert ingest.last_info["nd1"]["iccid"] == "8948"
