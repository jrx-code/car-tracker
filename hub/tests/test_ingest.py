"""Smoke tests for the parts that are easy to get wrong.

Run: python3 -m pytest tests/ -q
No broker and no network needed: the ingest layer is driven directly, exactly
the way the MQTT callback would drive it.
"""

from __future__ import annotations

import json
import sys
import time
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from tracker_hub.config import Config
from tracker_hub.ingest import Ingest, haversine_km, normalise_position
from tracker_hub.store import Store


@pytest.fixture()
def hub(tmp_path, monkeypatch):
    monkeypatch.setenv("MQTT_PASS", "test")
    monkeypatch.setenv("DB_PATH", str(tmp_path / "t.db"))
    cfg = Config()
    store = Store(cfg.db_path)
    ingest = Ingest(cfg, store)
    yield ingest, store
    store.close()


def pos_payload(seq, lat, lon, ts, mode="driving", speed=50.0, hdop=0.9):
    return json.dumps({
        "seq": seq, "ts": ts, "ts_src": "gnss", "lat": lat, "lon": lon,
        "alt": 30, "spd": speed, "crs": 90, "sat": 9, "hdop": hdop,
        "fix": 3, "st": mode, "src": "neo6m",
    }).encode()


def test_haversine_known_distance():
    # Szczecin to Berlin is about 130 km in a straight line.
    km = haversine_km(53.4285, 14.5528, 52.5200, 13.4050)
    assert 120 < km < 140


def test_compact_and_verbose_formats_agree():
    verbose = normalise_position({
        "seq": 1, "ts": 100, "lat": 53.4, "lon": 14.5, "spd": 42.0,
        "crs": 90, "sat": 9, "hdop": 0.8, "st": "driving", "src": "neo6m",
    })
    compact = normalise_position({
        "q": 1, "t": 100, "a": 53.4, "o": 14.5, "s": 42.0,
        "c": 90, "n": 9, "h": 0.8, "m": 1, "r": 0,
    })
    for key in ("seq", "ts", "lat", "lon", "speed", "course", "sat", "hdop", "mode", "src"):
        assert verbose[key] == compact[key], key


def test_position_stored_and_vehicle_created(hub):
    ingest, store = hub
    now = time.time()
    ingest._handle("cartracker/nd1/pos", pos_payload(1, 53.4, 14.5, now))
    assert store.vehicle("nd1") is not None
    assert store.last_position("nd1")["lat"] == pytest.approx(53.4)


def test_duplicate_seq_is_dropped(hub):
    """A backlog re-flush after a power cut must not double the track."""
    ingest, store = hub
    now = time.time()
    ingest._handle("cartracker/nd1/pos", pos_payload(7, 53.4, 14.5, now))
    ingest._handle("cartracker/nd1/pos", pos_payload(7, 53.4, 14.5, now))
    assert len(store.positions("nd1", since=0)) == 1


def test_implausible_jump_is_rejected(hub):
    ingest, store = hub
    now = time.time()
    ingest._handle("cartracker/nd1/pos", pos_payload(1, 53.4285, 14.5528, now))
    # 130 km one minute later would require 7800 km/h.
    ingest._handle("cartracker/nd1/pos", pos_payload(2, 52.5200, 13.4050, now + 60))
    assert len(store.positions("nd1", since=0)) == 1
    assert ingest.dropped == 1


def test_bad_hdop_is_rejected(hub):
    ingest, store = hub
    now = time.time()
    ingest._handle("cartracker/nd1/pos", pos_payload(1, 53.4, 14.5, now, hdop=9.9))
    assert store.last_position("nd1") is None


def test_trip_accumulates_distance(hub):
    ingest, store = hub
    now = time.time()
    ingest._handle("cartracker/nd1/evt", json.dumps(
        {"seq": 1, "ts": now, "ev": "trip_start", "lat": 53.40, "lon": 14.50}).encode())
    # Three points roughly 1.1 km apart in latitude.
    for i, lat in enumerate([53.40, 53.41, 53.42], start=2):
        ingest._handle("cartracker/nd1/pos", pos_payload(i, lat, 14.50, now + i * 30))
    ingest._handle("cartracker/nd1/evt", json.dumps(
        {"seq": 9, "ts": now + 300, "ev": "trip_end", "lat": 53.42, "lon": 14.50}).encode())

    trips = store.trips("nd1")
    assert len(trips) == 1
    assert trips[0]["open"] == 0
    assert 1.5 < trips[0]["distance_km"] < 3.0
    assert trips[0]["max_speed"] == pytest.approx(50.0)


def test_driving_position_without_trip_start_opens_a_trip(hub):
    """trip_start can be lost in a coverage hole; the drive must still count."""
    ingest, store = hub
    now = time.time()
    ingest._handle("cartracker/nd1/pos", pos_payload(1, 53.40, 14.50, now))
    assert store.open_trip("nd1") is not None


def test_batch_is_ordered_and_deduplicated(hub):
    ingest, store = hub
    now = time.time() - 3600
    points = [json.loads(pos_payload(100 + i, 53.40 + i * 0.001, 14.50, now + i * 30))
              for i in range(5)]
    batch = json.dumps({"n": len(points), "pts": list(reversed(points))}).encode()
    ingest._handle("cartracker/nd1/batch", batch)
    ingest._handle("cartracker/nd1/batch", batch)  # replay, as after a power cut

    stored = store.positions("nd1", since=0)
    assert len(stored) == 5
    assert stored == sorted(stored, key=lambda p: p["ts"])


def test_backlog_point_without_timestamp_is_dropped_not_restamped(hub):
    """Criterion 2 of the PoC: backlog points keep their original time or go."""
    ingest, store = hub
    old = time.time() - 3600
    good = json.loads(pos_payload(200, 53.40, 14.50, old))
    blind = json.loads(pos_payload(201, 53.401, 14.50, 0))
    batch = json.dumps({"n": 2, "pts": [good, blind]}).encode()
    ingest._handle("cartracker/nd1/batch", batch)

    stored = store.positions("nd1", since=0)
    assert [p["seq"] for p in stored] == [200]
    assert stored[0]["ts"] == pytest.approx(old)


def test_live_point_without_timestamp_gets_arrival_time(hub):
    ingest, store = hub
    ingest._handle("cartracker/nd1/pos", pos_payload(1, 53.40, 14.50, 0))
    assert store.last_position("nd1")["ts"] == pytest.approx(time.time(), abs=5)


def test_status_and_telemetry_update_the_vehicle(hub):
    ingest, store = hub
    ingest._handle("cartracker/nd3/status", b"online")
    ingest._handle("cartracker/nd3/tel", json.dumps({
        "seq": 1, "ts": time.time(), "vbat": 12.42, "rssi": -71, "net": "LTE",
        "op": "26006", "roam": False, "q": 3, "st": "parked",
    }).encode())
    v = store.vehicle("nd3")
    assert v["online"] == 1
    assert v["voltage"] == pytest.approx(12.42)
    assert v["queued"] == 3
    assert len(store.telemetry("nd3", 0)) == 1


def test_two_vehicles_stay_separate(hub):
    """The whole point of the hub: more than one tracker, no cross-talk."""
    ingest, store = hub
    now = time.time()
    ingest._handle("cartracker/nd1/pos", pos_payload(1, 53.40, 14.50, now))
    ingest._handle("cartracker/nd3/pos", pos_payload(1, 52.20, 21.00, now))

    assert {v["vehicle_id"] for v in store.vehicles()} == {"nd1", "nd3"}
    assert store.last_position("nd1")["lat"] == pytest.approx(53.40)
    assert store.last_position("nd3")["lat"] == pytest.approx(52.20)
    # Same seq on both vehicles is not a duplicate.
    assert len(store.positions("nd1", since=0)) == 1
    assert len(store.positions("nd3", since=0)) == 1


def test_garbage_payload_does_not_raise(hub):
    ingest, store = hub
    ingest._handle("cartracker/nd1/pos", b"{not json")
    ingest._handle("cartracker/nd1/pos", b"[1,2,3]")
    ingest._handle("cartracker/nd1/pos", b"{}")
    assert store.last_position("nd1") is None
    assert ingest.dropped >= 2


def test_retention_keeps_trips_and_drops_old_positions(hub):
    ingest, store = hub
    old = time.time() - 400 * 86400
    ingest._handle("cartracker/nd1/evt", json.dumps(
        {"seq": 1, "ts": old, "ev": "trip_start", "lat": 53.4, "lon": 14.5}).encode())
    ingest._handle("cartracker/nd1/pos", pos_payload(2, 53.4, 14.5, old))

    store.purge_old_positions(180)
    assert store.positions("nd1", since=0) == []
    assert len(store.trips("nd1")) == 1


def test_info_stores_the_identity_the_device_reports(hub):
    ingest, store = hub
    ingest._handle("cartracker/nd1/info", json.dumps({
        "name": "MX-5 ND1", "plate": "ZS12345", "vin": "JM1NB353820123456",
        "fw": "0.1.0", "modem": "none-wifi", "imei": "x", "ip": "10.0.40.39",
    }).encode())
    v = store.vehicle("nd1")
    assert v["name"] == "MX-5 ND1"
    assert v["plate"] == "ZS12345"
    assert v["vin"] == "JM1NB353820123456"


def test_info_without_identity_does_not_wipe_a_known_one(hub):
    """A device on older firmware must not blank what a newer one reported."""
    ingest, store = hub
    ingest._handle("cartracker/nd1/info", json.dumps(
        {"plate": "ZS12345", "vin": "JM1NB353820123456"}).encode())
    ingest._handle("cartracker/nd1/info", json.dumps({"fw": "0.0.9"}).encode())
    v = store.vehicle("nd1")
    assert v["plate"] == "ZS12345"
    assert v["vin"] == "JM1NB353820123456"


def test_plate_and_vin_columns_are_added_to_an_existing_database(tmp_path):
    """CREATE TABLE IF NOT EXISTS never adds a column to a table that exists."""
    import sqlite3

    path = str(tmp_path / "old.db")
    old = sqlite3.connect(path)
    old.execute("CREATE TABLE vehicles (vehicle_id TEXT PRIMARY KEY, name TEXT)")
    old.commit()
    old.close()

    store = Store(path)
    have = {r[1] for r in store._db.execute("PRAGMA table_info(vehicles)")}
    assert {"plate", "vin"} <= have
    store.close()


def test_discovery_announces_every_entity_once_per_identity(hub):
    """Configs are retained, so republishing on every packet would be noise."""
    ingest, _ = hub
    sent = []
    ingest._raw_publish = lambda t, p, retain=False: sent.append((t, p, retain))
    ingest.discovery._publish = ingest._raw_publish

    ingest._handle("cartracker/nd1/info", json.dumps(
        {"name": "MX-5 ND1", "plate": "ZS12345", "fw": "0.1.0"}).encode())
    first = len(sent)
    assert first == 21, f"expected 21 configs, got {first}"
    assert all(retain for _, _, retain in sent)

    # Same identity again: nothing new on the wire.
    ingest._handle("cartracker/nd1/info", json.dumps(
        {"name": "MX-5 ND1", "plate": "ZS12345", "fw": "0.1.0"}).encode())
    assert len(sent) == first

    # A firmware bump is an identity change, because it shows in the device block.
    ingest._handle("cartracker/nd1/info", json.dumps(
        {"name": "MX-5 ND1", "plate": "ZS12345", "fw": "0.2.0"}).encode())
    assert len(sent) == first * 2


def test_direct_entities_do_not_read_hub_topics(hub):
    """The whole point: a dead hub must not take battery voltage with it."""
    ingest, _ = hub
    sent = {}
    ingest.discovery._publish = lambda t, p, retain=False: sent.__setitem__(t, p)
    ingest._handle("cartracker/nd1/info", json.dumps({"fw": "0.1.0"}).encode())

    hub_sourced = set()
    for payload in sent.values():
        cfg = json.loads(payload)
        state = cfg.get("state_topic")
        if state and state.endswith("/trip"):
            hub_sourced.add(cfg["unique_id"])

    voltage = json.loads(sent["homeassistant/sensor/cartracker_nd1/voltage/config"])
    assert voltage["state_topic"] == "cartracker/nd1/tel"
    assert voltage["unique_id"] not in hub_sourced
    # Only the four trip sensors are allowed to depend on this process.
    assert len(hub_sourced) == 4


def test_trip_state_is_published_and_retained(hub):
    ingest, _ = hub
    sent = {}
    ingest.discovery._publish = lambda t, p, retain=False: sent.__setitem__(
        t, (p, retain))

    now = time.time()
    ingest._handle("cartracker/nd1/evt", json.dumps(
        {"ts": now - 600, "ev": "trip_start", "lat": 53.4, "lon": 14.5}).encode())
    ingest._handle("cartracker/nd1/pos", pos_payload(1, 53.41, 14.51, now - 300))
    ingest._handle("cartracker/nd1/pos", pos_payload(2, 53.42, 14.52, now))

    payload, retain = sent["cartracker/nd1/trip"]
    trip = json.loads(payload)
    assert retain is True
    assert trip["open"] is True
    assert trip["distance_km"] > 0
    assert trip["duration_s"] >= 590


def test_tracker_has_no_availability_so_a_lost_car_stays_on_the_map(hub):
    ingest, _ = hub
    sent = {}
    ingest.discovery._publish = lambda t, p, retain=False: sent.__setitem__(t, p)
    ingest._handle("cartracker/nd1/info", json.dumps({"fw": "0.1.0"}).encode())

    tracker = json.loads(sent["homeassistant/device_tracker/cartracker_nd1/tracker/config"])
    assert "availability_topic" not in tracker
    # Every other entity keeps it: an offline device must not show stale volts.
    voltage = json.loads(sent["homeassistant/sensor/cartracker_nd1/voltage/config"])
    assert voltage["availability_topic"] == "cartracker/nd1/status"


def test_retained_position_does_not_open_a_trip(hub):
    """The broker replays the last drive on every reconnect."""
    ingest, store = hub
    ingest._handle("cartracker/nd1/pos",
                   pos_payload(1, 53.4, 14.5, time.time(), mode="driving"),
                   retained=True)
    assert store.open_trip("nd1") is None
    # The same packet live is a real drive and does open one.
    ingest._handle("cartracker/nd1/pos",
                   pos_payload(2, 53.4, 14.5, time.time(), mode="driving"))
    assert store.open_trip("nd1") is not None


def test_retained_telemetry_is_not_stored_twice(hub):
    ingest, store = hub
    payload = json.dumps({"ts": time.time(), "vbat": 12.6, "rssi": -70,
                          "q": 0, "st": "parked"}).encode()
    ingest._handle("cartracker/nd1/tel", payload)
    ingest._handle("cartracker/nd1/tel", payload, retained=True)
    assert len(store.telemetry("nd1", 0)) == 1
    # The summary still reflects it, so the fleet page is not blank after a restart.
    assert store.vehicle("nd1")["voltage"] == 12.6
