#!/usr/bin/env python3
"""Fill the database with two demo vehicles, for developing the UI offline.

    python3 tools_seed.py /tmp/demo.db
    DB_PATH=/tmp/demo.db MQTT_PASS=x python3 -m tracker_hub
"""
from __future__ import annotations

import json
import math
import sys
import time

sys.path.insert(0, ".")
# Same workstation-only guard as tests/conftest.py: a broken pyOpenSSL makes
# paho's optional `import dns.resolver` raise AttributeError instead of
# ImportError. Blocking the optional dependency keeps this runnable locally.
sys.modules.setdefault("dns", None)
from tracker_hub.config import Config
from tracker_hub.ingest import Ingest
from tracker_hub.store import Store

db = sys.argv[1] if len(sys.argv) > 1 else "/tmp/tracker-demo.db"
import os

os.environ.setdefault("MQTT_PASS", "seed")
os.environ["DB_PATH"] = db

cfg = Config()
store = Store(cfg.db_path)
ingest = Ingest(cfg, store)

now = time.time()


def pos(seq, lat, lon, ts, mode="driving", speed=52.0):
    return json.dumps({
        "seq": seq, "ts": ts, "lat": lat, "lon": lon, "spd": speed, "crs": 90,
        "sat": 9, "hdop": 0.9, "fix": 3, "st": mode, "src": "neo6m",
    }).encode()


# ND3: a finished trip plus a live one.
ingest._handle("cartracker/nd3/status", b"online")
ingest._handle("cartracker/nd3/info", json.dumps(
    {"fw": "0.1.0", "modem": "none-wifi", "imei": "seed"}).encode())
ingest._handle("cartracker/nd3/evt", json.dumps(
    {"ts": now - 7200, "ev": "trip_start", "lat": 53.4280, "lon": 14.5528}).encode())
lat, lon = 53.4280, 14.5528
for i in range(40):
    lat += 0.0015 * math.cos(i / 6)
    lon += 0.0022 * math.sin(i / 6) + 0.0009
    ingest._handle("cartracker/nd3/pos", pos(1000 + i, lat, lon, now - 7200 + i * 30))
ingest._handle("cartracker/nd3/evt", json.dumps(
    {"ts": now - 6000, "ev": "trip_end", "lat": lat, "lon": lon}).encode())
ingest._handle("cartracker/nd3/tel", json.dumps(
    {"ts": now, "vbat": 12.62, "rssi": -68, "net": "LTE", "op": "26006",
     "roam": False, "q": 0, "st": "parked"}).encode())

# ND1: the seasonal car, parked for weeks with a slowly sagging battery.
ingest._handle("cartracker/nd1/status", b"online")
ingest._handle("cartracker/nd1/pos", pos(1, 53.4285, 14.5528, now - 86400 * 12, "parked", 0.0))
voltage = 12.75
for day in range(21):
    voltage -= 0.017
    ingest._handle("cartracker/nd1/tel", json.dumps(
        {"ts": now - (21 - day) * 86400, "vbat": round(voltage, 2), "rssi": -79,
         "net": "LTE", "q": 0, "st": "parked"}).encode())
ingest._handle("cartracker/nd1/evt", json.dumps(
    {"ts": now - 3600, "ev": "battery_low", "lat": 53.4285, "lon": 14.5528}).encode())

print(f"seeded {db}: {store.stats()}")
store.close()
