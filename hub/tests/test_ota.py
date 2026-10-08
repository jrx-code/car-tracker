"""Firmware over MQTT: staging, chunk serving, routing and the upload endpoint."""

from __future__ import annotations

import base64
import hashlib
import json
import struct
import sys
import threading
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tracker_hub import web
from tracker_hub.config import Config
from tracker_hub.ingest import Ingest
from tracker_hub.ota import OtaError, OtaServer
from tracker_hub.store import Store

IMAGE = bytes([0xE9]) + bytes(range(256)) * 10  # 2561 bytes, ESP32 magic first
SIG = base64.b64encode(b"\x30" + b"\x01" * 70).decode()


@pytest.fixture()
def server(tmp_path):
    sent_bytes, commands = [], []
    srv = OtaServer(str(tmp_path / "fw"), "cartracker",
                    lambda t, p: sent_bytes.append((t, p)),
                    lambda vid, cmd, **kw: commands.append((vid, cmd, kw)) or True)
    return srv, sent_bytes, commands


def test_stage_sends_signed_command(server):
    srv, _, commands = server
    meta = srv.stage("nd1", IMAGE, SIG, chunk=1024)
    assert meta["sha256"] == hashlib.sha256(IMAGE).hexdigest()
    assert commands == [("nd1", "ota", {
        "size": len(IMAGE), "sha256": meta["sha256"], "sig": SIG, "chunk": 1024})]


def test_chunks_carry_their_offset(server):
    srv, sent, _ = server
    srv.stage("nd1", IMAGE, SIG, chunk=1024)
    assert srv.on_request("nd1", {"off": 2048, "len": 513})
    topic, payload = sent[-1]
    assert topic == "cartracker/nd1/ota/data"
    assert struct.unpack("<I", payload[:4])[0] == 2048
    assert payload[4:] == IMAGE[2048:2561]
    assert srv.status("nd1")["served"] == 1


@pytest.mark.parametrize("req", [
    {"off": -1, "len": 10},
    {"off": 0, "len": 0},
    {"off": 0, "len": 4096},          # larger than a chunk may be
    {"off": 2560, "len": 2},          # runs past the end of the image
    {"off": "x", "len": 10},
])
def test_bad_chunk_requests_are_ignored(server, req):
    srv, sent, _ = server
    srv.stage("nd1", IMAGE, SIG)
    assert not srv.on_request("nd1", req)
    assert sent == []


def test_nothing_staged_nothing_served(server):
    srv, sent, _ = server
    assert not srv.on_request("nd3", {"off": 0, "len": 16})
    assert sent == []


@pytest.mark.parametrize("vehicle,image,sig,why", [
    ("nd1", b"\x00" + IMAGE[1:], SIG, "ESP32"),
    ("nd1", IMAGE, "not base64!", "base64"),
    ("nd1", IMAGE, base64.b64encode(b"x").decode(), "length"),
    ("../etc", IMAGE, SIG, "vehicle"),
    ("nd1", b"", SIG, "bytes"),
])
def test_stage_refuses_bad_input(server, vehicle, image, sig, why):
    srv, _, commands = server
    with pytest.raises(OtaError, match=why):
        srv.stage(vehicle, image, sig)
    assert commands == []


def test_ingest_routes_requests_and_ignores_its_own_data(tmp_path, monkeypatch):
    monkeypatch.setenv("MQTT_PASS", "test")
    monkeypatch.setenv("DB_PATH", str(tmp_path / "t.db"))
    cfg = Config()
    ingest = Ingest(cfg, Store(cfg.db_path))
    sent = []
    monkeypatch.setattr(ingest.ota, "_publish_bytes", lambda t, p: sent.append((t, p)))
    monkeypatch.setattr(ingest.ota, "_publish_command", lambda *a, **k: True)
    ingest.ota.stage("nd1", IMAGE, SIG)

    ingest._handle("cartracker/nd1/ota/req", json.dumps({"off": 0, "len": 256}).encode())
    assert len(sent) == 1 and sent[0][1][4:] == IMAGE[:256]
    # The hub's own chunk comes back through the cartracker/+/# subscription.
    ingest._handle("cartracker/nd1/ota/data", sent[0][1])
    assert len(sent) == 1


def _post(base, path, data, headers):
    req = Request(base + path, method="POST", data=data)
    for k, v in headers.items():
        req.add_header(k, v)
    try:
        with urlopen(req, timeout=5) as resp:
            return resp.status, json.loads(resp.read())
    except HTTPError as exc:
        return exc.code, json.loads(exc.read() or b"{}")


def test_upload_endpoint_needs_auth_and_stages(tmp_path, monkeypatch):
    monkeypatch.setenv("MQTT_PASS", "test")
    monkeypatch.setenv("DB_PATH", str(tmp_path / "t.db"))
    monkeypatch.setenv("HTTP_HOST", "127.0.0.1")
    monkeypatch.setenv("HTTP_PORT", "0")
    monkeypatch.setattr(web, "API_TOKEN", "tok")
    cfg = Config()
    store = Store(cfg.db_path)
    ingest = Ingest(cfg, store)
    commands = []
    monkeypatch.setattr(ingest.ota, "_publish_command",
                        lambda vid, cmd, **kw: commands.append((vid, cmd)) or True)
    store.upsert_vehicle("nd1", last_seen=1.0)
    hub = web.serve(cfg, store, ingest)
    threading.Thread(target=hub.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{hub.server_address[1]}"
    try:
        hdr = {"Content-Type": "application/octet-stream", "X-Firmware-Signature": SIG}
        status, _ = _post(base, "/api/vehicles/nd1/ota", IMAGE, hdr)
        assert status == 403 and commands == []

        status, body = _post(base, "/api/vehicles/nd1/ota", IMAGE, {**hdr, "X-Api-Token": "tok"})
        assert status == 200 and body["size"] == len(IMAGE) and commands == [("nd1", "ota")]

        status, _ = _post(base, "/api/vehicles/zz9/ota", IMAGE, {**hdr, "X-Api-Token": "tok"})
        assert status == 404
    finally:
        hub.shutdown()
        store.close()
