#!/usr/bin/env python3
"""Telemetry simulator: plays a fake trip onto the broker.

Used in phase 0 of docs/11-plan-wdrozenia.md to exercise the HA integration
before any hardware exists, and later to reproduce field problems on the bench.

Standard library only, no external deps beyond paho-mqtt, which is already
required by anything talking to the broker.

    ./sim_track.py --vehicle nd1 --trip           # drive a loop around Szczecin
    ./sim_track.py --vehicle nd1 --park           # parked telemetry only
    ./sim_track.py --vehicle nd1 --alarm          # motion while parked
    ./sim_track.py --vehicle nd1 --backlog 120    # flush a fake offline backlog
"""

from __future__ import annotations

import argparse
import json
import math
import os
import ssl
import sys
import time

try:
    import paho.mqtt.client as mqtt
except ImportError:  # pragma: no cover
    sys.exit("pip install paho-mqtt")

DEFAULT_HOST = "mqtt.example.lan"
DEFAULT_PORT = 8883

# Start point of the simulated trip.
START_LAT, START_LON = 52.000000, 21.000000


def build_client(args: argparse.Namespace, lwt_topic: str) -> mqtt.Client:
    client = mqtt.Client(
        mqtt.CallbackAPIVersion.VERSION2, client_id=f"sim-{args.vehicle}"
    )
    password = args.password or os.environ.get("MQTT_PASS")
    if not password:
        sys.exit("set --password or MQTT_PASS (menedzer hasel, never hardcode)")
    client.username_pw_set(args.user, password)
    client.will_set(lwt_topic, "offline", qos=1, retain=True)
    if not args.insecure:
        client.tls_set(cert_reqs=ssl.CERT_REQUIRED)
    else:
        client.tls_set(cert_reqs=ssl.CERT_NONE)
        client.tls_insecure_set(True)
    client.connect(args.host, args.port, keepalive=60)
    client.loop_start()
    return client


def move(lat: float, lon: float, bearing_deg: float, metres: float) -> tuple[float, float]:
    """Move a point along a bearing. Flat earth is good enough over 30 s of driving."""
    d_lat = metres * math.cos(math.radians(bearing_deg)) / 111_320.0
    d_lon = (
        metres
        * math.sin(math.radians(bearing_deg))
        / (111_320.0 * math.cos(math.radians(lat)))
    )
    return lat + d_lat, lon + d_lon


def pos_payload(seq: int, lat: float, lon: float, speed: float, course: int,
                mode: str, ts: float | None = None) -> str:
    return json.dumps(
        {
            "seq": seq,
            "ts": int(ts or time.time()),
            "ts_src": "gnss",
            "lat": round(lat, 7),
            "lon": round(lon, 7),
            "alt": 31,
            "spd": round(speed, 1),
            "crs": course % 360,
            "sat": 9,
            "hdop": 0.9,
            "fix": 3,
            "st": mode,
            "src": "neo6m",
        }
    )


def tel_payload(seq: int, voltage: float, mode: str, queued: int = 0) -> str:
    return json.dumps(
        {
            "seq": seq,
            "ts": int(time.time()),
            "vbat": round(voltage, 2),
            "vsys": 3.31,
            "rssi": -71,
            "net": "LTE",
            "op": "26006",
            "roam": False,
            "up": int(time.monotonic()),
            "q": queued,
            "rst": 3,
            "st": mode,
        }
    )


def run_trip(client: mqtt.Client, base: str, args: argparse.Namespace) -> None:
    lat, lon = START_LAT, START_LON
    course = 45
    seq = 1
    client.publish(f"{base}/evt", json.dumps(
        {"seq": seq, "ts": int(time.time()), "ev": "trip_start", "lat": lat, "lon": lon}
    ), qos=1)

    for step in range(args.points):
        seq += 1
        speed = 50 + 20 * math.sin(step / 4)
        course = (course + (15 if step % 7 == 0 else 0)) % 360
        lat, lon = move(lat, lon, course, speed * 1000 / 3600 * args.interval)
        client.publish(f"{base}/pos", pos_payload(seq, lat, lon, speed, course, "driving"), qos=1)
        print(f"pos {seq}: {lat:.5f},{lon:.5f} {speed:.0f} km/h crs {course}")
        if step % 2 == 0:
            seq += 1
            client.publish(f"{base}/tel", tel_payload(seq, 14.1, "driving"), qos=1)
        time.sleep(args.interval if not args.fast else 0.2)

    seq += 1
    client.publish(f"{base}/evt", json.dumps(
        {"seq": seq, "ts": int(time.time()), "ev": "trip_end", "lat": lat, "lon": lon}
    ), qos=1)
    print("trip finished")


def run_park(client: mqtt.Client, base: str, args: argparse.Namespace) -> None:
    seq = 1
    voltage = 12.7
    while True:
        client.publish(f"{base}/tel", tel_payload(seq, voltage, "parked"), qos=1)
        print(f"tel {seq}: {voltage:.2f} V")
        voltage = max(11.6, voltage - args.drain)
        seq += 1
        time.sleep(args.interval if not args.fast else 1)


def run_alarm(client: mqtt.Client, base: str, args: argparse.Namespace) -> None:
    """Movement with the engine off, the case the firmware decides on its own."""
    lat, lon = START_LAT, START_LON
    seq = 1
    client.publish(f"{base}/evt", json.dumps(
        {"seq": seq, "ts": int(time.time()), "ev": "motion_alarm", "lat": lat, "lon": lon}
    ), qos=1)
    for step in range(args.points):
        seq += 1
        lat, lon = move(lat, lon, 180, 8)
        client.publish(f"{base}/pos", pos_payload(seq, lat, lon, 3.0, 180, "moved"), qos=1)
        print(f"alarm pos {seq}: {lat:.5f},{lon:.5f}")
        time.sleep(1 if args.fast else args.interval)


def run_backlog(client: mqtt.Client, base: str, args: argparse.Namespace) -> None:
    """A batch of points with past timestamps, as after a coverage gap."""
    lat, lon = START_LAT, START_LON
    now = time.time()
    points = []
    for i in range(args.backlog):
        lat, lon = move(lat, lon, 90, 400)
        points.append(
            json.loads(
                pos_payload(
                    10_000 + i, lat, lon, 48.0, 90, "driving",
                    ts=now - (args.backlog - i) * 30,
                )
            )
        )
    for chunk_start in range(0, len(points), 50):
        chunk = points[chunk_start : chunk_start + 50]
        client.publish(
            f"{base}/batch", json.dumps({"n": len(chunk), "pts": chunk}), qos=1
        )
        print(f"batch of {len(chunk)} sent")
        time.sleep(0.5)
    # Sending the same batch twice on purpose: the integration must deduplicate
    # by seq, because that is the normal outcome of a power cut mid-flush.
    if args.duplicate:
        client.publish(
            f"{base}/batch", json.dumps({"n": len(points[:50]), "pts": points[:50]}), qos=1
        )
        print("duplicate batch sent, HA should ignore it")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--vehicle", default="nd1")
    p.add_argument("--prefix", default="cartracker")
    p.add_argument("--host", default=DEFAULT_HOST)
    p.add_argument("--port", type=int, default=DEFAULT_PORT)
    p.add_argument("--user", default=None, help="defaults to cartracker-<vehicle>")
    p.add_argument("--password", default=None, help="or MQTT_PASS env var")
    p.add_argument("--insecure", action="store_true", help="skip TLS verification")
    p.add_argument("--interval", type=float, default=30.0)
    p.add_argument("--points", type=int, default=20)
    p.add_argument("--drain", type=float, default=0.05, help="V lost per park tick")
    p.add_argument("--fast", action="store_true", help="compress time")
    p.add_argument("--duplicate", action="store_true", help="resend one batch")
    mode = p.add_mutually_exclusive_group(required=True)
    mode.add_argument("--trip", action="store_true")
    mode.add_argument("--park", action="store_true")
    mode.add_argument("--alarm", action="store_true")
    mode.add_argument("--backlog", type=int, metavar="N")
    args = p.parse_args()

    if args.user is None:
        args.user = f"cartracker-{args.vehicle}"

    base = f"{args.prefix}/{args.vehicle}"
    client = build_client(args, f"{base}/status")
    client.publish(f"{base}/status", "online", qos=1, retain=True)
    client.publish(
        f"{base}/info",
        json.dumps({"fw": "sim", "modem": "simulator", "imei": "0", "net": "LTE"}),
        qos=1,
        retain=True,
    )

    try:
        if args.trip:
            run_trip(client, base, args)
        elif args.park:
            run_park(client, base, args)
        elif args.alarm:
            run_alarm(client, base, args)
        else:
            run_backlog(client, base, args)
    except KeyboardInterrupt:
        pass
    finally:
        client.publish(f"{base}/status", "offline", qos=1, retain=True)
        time.sleep(0.5)
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
