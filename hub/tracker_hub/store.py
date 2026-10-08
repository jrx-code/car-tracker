"""SQLite storage.

One database, one row per position, one row per trip. SQLite and not InfluxDB
because this service owns its own history and must keep working when Influx is
down; the interesting queries here are "last position of each vehicle" and
"trips of vehicle X", both of which are plain indexed lookups.

Every write goes through this module, and every method is safe to call from the
MQTT thread and the HTTP thread at the same time (one lock, short transactions).
"""

from __future__ import annotations

import logging
import sqlite3
import threading
import time
from pathlib import Path
from typing import Any

_LOG = logging.getLogger(__name__)

SCHEMA_VERSION = 1

_SCHEMA = """
CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS vehicles (
    vehicle_id   TEXT PRIMARY KEY,
    name         TEXT,
    plate        TEXT,
    vin          TEXT,
    online       INTEGER NOT NULL DEFAULT 0,
    mode         TEXT,
    firmware     TEXT,
    modem        TEXT,
    imei         TEXT,
    last_seen    REAL,
    last_status  REAL,
    voltage      REAL,
    rssi         INTEGER,
    network      TEXT,
    operator     TEXT,
    roaming      INTEGER NOT NULL DEFAULT 0,
    queued       INTEGER NOT NULL DEFAULT 0,
    uptime       INTEGER,
    ip           TEXT,
    first_seen   REAL
);

CREATE TABLE IF NOT EXISTS positions (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    vehicle_id TEXT NOT NULL,
    seq        INTEGER,
    ts         REAL NOT NULL,
    lat        REAL NOT NULL,
    lon        REAL NOT NULL,
    alt        REAL,
    speed      REAL,
    course     INTEGER,
    sat        INTEGER,
    hdop       REAL,
    mode       TEXT,
    src        TEXT,
    trip_id    INTEGER,
    received   REAL NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_pos_vehicle_ts ON positions(vehicle_id, ts);
CREATE INDEX IF NOT EXISTS idx_pos_trip ON positions(trip_id);
-- Deduplication of the backlog flush: a record leaves the device queue only
-- after PUBACK, so a power cut mid-flush resends it (car-tracker docs/02 2.6).
CREATE UNIQUE INDEX IF NOT EXISTS idx_pos_vehicle_seq
    ON positions(vehicle_id, seq) WHERE seq IS NOT NULL;

CREATE TABLE IF NOT EXISTS trips (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    vehicle_id   TEXT NOT NULL,
    started      REAL NOT NULL,
    ended        REAL,
    start_lat    REAL,
    start_lon    REAL,
    end_lat      REAL,
    end_lon      REAL,
    distance_km  REAL NOT NULL DEFAULT 0,
    max_speed    REAL NOT NULL DEFAULT 0,
    avg_speed    REAL NOT NULL DEFAULT 0,
    samples      INTEGER NOT NULL DEFAULT 0,
    open         INTEGER NOT NULL DEFAULT 1
);
CREATE INDEX IF NOT EXISTS idx_trip_vehicle ON trips(vehicle_id, started DESC);

CREATE TABLE IF NOT EXISTS events (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    vehicle_id TEXT NOT NULL,
    ts         REAL NOT NULL,
    event      TEXT NOT NULL,
    lat        REAL,
    lon        REAL,
    payload    TEXT
);
CREATE INDEX IF NOT EXISTS idx_event_vehicle_ts ON events(vehicle_id, ts DESC);

CREATE TABLE IF NOT EXISTS telemetry (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    vehicle_id TEXT NOT NULL,
    ts         REAL NOT NULL,
    voltage    REAL,
    rssi       INTEGER,
    queued     INTEGER,
    mode       TEXT
);
CREATE INDEX IF NOT EXISTS idx_tel_vehicle_ts ON telemetry(vehicle_id, ts DESC);
"""


class Store:
    def __init__(self, path: str) -> None:
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        # check_same_thread=False plus an explicit lock: the MQTT callback thread
        # and the HTTP handler threads share one connection.
        self._db = sqlite3.connect(path, check_same_thread=False)
        self._db.row_factory = sqlite3.Row
        self._lock = threading.Lock()
        with self._lock:
            self._db.executescript(_SCHEMA)
            # WAL keeps readers from blocking the MQTT writer.
            self._db.execute("PRAGMA journal_mode=WAL")
            self._db.execute("PRAGMA synchronous=NORMAL")
            self._migrate()
            self._db.execute(
                "INSERT OR REPLACE INTO meta(key, value) VALUES('schema', ?)",
                (str(SCHEMA_VERSION),),
            )
            self._db.commit()

    def _migrate(self) -> None:
        """Add columns that CREATE TABLE IF NOT EXISTS will never add.

        On an existing database the CREATE statements are no-ops, so a column
        added to the schema string simply never appears and every write that
        mentions it fails at runtime. That is exactly how the `ip` column went
        missing: the table was there, the column was not, and the only symptom
        was an exception buried in the MQTT handler.
        """
        wanted = {"vehicles": {"ip": "TEXT", "plate": "TEXT", "vin": "TEXT"}}
        for table, columns in wanted.items():
            have = {row[1] for row in self._db.execute(f"PRAGMA table_info({table})")}
            for name, decl in columns.items():
                if name not in have:
                    _LOG.info("migrating: adding %s.%s", table, name)
                    self._db.execute(f"ALTER TABLE {table} ADD COLUMN {name} {decl}")

    def close(self) -> None:
        with self._lock:
            self._db.close()

    # --- vehicles ---------------------------------------------------------

    def upsert_vehicle(self, vehicle_id: str, **fields: Any) -> None:
        allowed = {
            "name", "plate", "vin", "online", "mode", "firmware", "modem",
            "imei", "last_seen", "last_status", "voltage", "rssi", "network",
            "operator", "roaming", "queued", "uptime", "ip",
        }
        data = {k: v for k, v in fields.items() if k in allowed and v is not None}
        now = time.time()
        with self._lock:
            self._db.execute(
                "INSERT OR IGNORE INTO vehicles(vehicle_id, first_seen) VALUES(?, ?)",
                (vehicle_id, now),
            )
            if data:
                sets = ", ".join(f"{k} = ?" for k in data)
                self._db.execute(
                    f"UPDATE vehicles SET {sets} WHERE vehicle_id = ?",
                    (*data.values(), vehicle_id),
                )
            self._db.commit()

    def vehicles(self) -> list[dict[str, Any]]:
        with self._lock:
            rows = self._db.execute(
                "SELECT * FROM vehicles ORDER BY vehicle_id"
            ).fetchall()
        return [dict(r) for r in rows]

    def vehicle(self, vehicle_id: str) -> dict[str, Any] | None:
        with self._lock:
            row = self._db.execute(
                "SELECT * FROM vehicles WHERE vehicle_id = ?", (vehicle_id,)
            ).fetchone()
        return dict(row) if row else None

    # --- positions --------------------------------------------------------

    def insert_position(self, vehicle_id: str, pos: dict[str, Any],
                        trip_id: int | None) -> bool:
        """Returns False when the row was a duplicate (same vehicle and seq)."""
        with self._lock:
            try:
                self._db.execute(
                    "INSERT INTO positions(vehicle_id, seq, ts, lat, lon, alt, speed,"
                    " course, sat, hdop, mode, src, trip_id, received)"
                    " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                    (
                        vehicle_id, pos.get("seq"), pos["ts"], pos["lat"], pos["lon"],
                        pos.get("alt"), pos.get("speed"), pos.get("course"),
                        pos.get("sat"), pos.get("hdop"), pos.get("mode"),
                        pos.get("src"), trip_id, time.time(),
                    ),
                )
                self._db.commit()
                return True
            except sqlite3.IntegrityError:
                # Expected on a backlog re-flush, not an error worth logging loudly.
                return False

    def last_position(self, vehicle_id: str) -> dict[str, Any] | None:
        with self._lock:
            row = self._db.execute(
                "SELECT * FROM positions WHERE vehicle_id = ? ORDER BY ts DESC LIMIT 1",
                (vehicle_id,),
            ).fetchone()
        return dict(row) if row else None

    def positions(self, vehicle_id: str, since: float | None = None,
                  until: float | None = None, trip_id: int | None = None,
                  limit: int = 5000) -> list[dict[str, Any]]:
        sql = "SELECT * FROM positions WHERE vehicle_id = ?"
        args: list[Any] = [vehicle_id]
        if trip_id is not None:
            sql += " AND trip_id = ?"
            args.append(trip_id)
        if since is not None:
            sql += " AND ts >= ?"
            args.append(since)
        if until is not None:
            sql += " AND ts <= ?"
            args.append(until)
        sql += " ORDER BY ts ASC LIMIT ?"
        args.append(limit)
        with self._lock:
            rows = self._db.execute(sql, args).fetchall()
        return [dict(r) for r in rows]

    # --- trips ------------------------------------------------------------

    def open_trip(self, vehicle_id: str) -> dict[str, Any] | None:
        with self._lock:
            row = self._db.execute(
                "SELECT * FROM trips WHERE vehicle_id = ? AND open = 1"
                " ORDER BY started DESC LIMIT 1",
                (vehicle_id,),
            ).fetchone()
        return dict(row) if row else None

    def start_trip(self, vehicle_id: str, started: float, lat: float | None,
                   lon: float | None) -> int:
        with self._lock:
            # Never two open trips for one vehicle: a missed trip_end would
            # otherwise keep accumulating into a trip that ended days ago.
            self._db.execute(
                "UPDATE trips SET open = 0 WHERE vehicle_id = ? AND open = 1",
                (vehicle_id,),
            )
            cur = self._db.execute(
                "INSERT INTO trips(vehicle_id, started, start_lat, start_lon, open)"
                " VALUES(?,?,?,?,1)",
                (vehicle_id, started, lat, lon),
            )
            self._db.commit()
            return int(cur.lastrowid)

    def update_trip(self, trip_id: int, **fields: Any) -> None:
        allowed = {
            "ended", "end_lat", "end_lon", "distance_km", "max_speed", "avg_speed",
            "samples", "open",
        }
        data = {k: v for k, v in fields.items() if k in allowed and v is not None}
        if not data:
            return
        sets = ", ".join(f"{k} = ?" for k in data)
        with self._lock:
            self._db.execute(
                f"UPDATE trips SET {sets} WHERE id = ?", (*data.values(), trip_id)
            )
            self._db.commit()

    def trips(self, vehicle_id: str | None = None, limit: int = 50) -> list[dict[str, Any]]:
        sql = "SELECT * FROM trips"
        args: list[Any] = []
        if vehicle_id:
            sql += " WHERE vehicle_id = ?"
            args.append(vehicle_id)
        sql += " ORDER BY started DESC LIMIT ?"
        args.append(limit)
        with self._lock:
            rows = self._db.execute(sql, args).fetchall()
        return [dict(r) for r in rows]

    def trip(self, trip_id: int) -> dict[str, Any] | None:
        with self._lock:
            row = self._db.execute("SELECT * FROM trips WHERE id = ?", (trip_id,)).fetchone()
        return dict(row) if row else None

    # --- events and telemetry --------------------------------------------

    def insert_event(self, vehicle_id: str, ts: float, event: str,
                     lat: float | None, lon: float | None, payload: str) -> None:
        with self._lock:
            self._db.execute(
                "INSERT INTO events(vehicle_id, ts, event, lat, lon, payload)"
                " VALUES(?,?,?,?,?,?)",
                (vehicle_id, ts, event, lat, lon, payload),
            )
            self._db.commit()

    def events(self, vehicle_id: str | None = None, limit: int = 100) -> list[dict[str, Any]]:
        sql = "SELECT * FROM events"
        args: list[Any] = []
        if vehicle_id:
            sql += " WHERE vehicle_id = ?"
            args.append(vehicle_id)
        sql += " ORDER BY ts DESC LIMIT ?"
        args.append(limit)
        with self._lock:
            rows = self._db.execute(sql, args).fetchall()
        return [dict(r) for r in rows]

    def insert_telemetry(self, vehicle_id: str, ts: float, voltage: float | None,
                         rssi: int | None, queued: int | None, mode: str | None) -> None:
        with self._lock:
            self._db.execute(
                "INSERT INTO telemetry(vehicle_id, ts, voltage, rssi, queued, mode)"
                " VALUES(?,?,?,?,?,?)",
                (vehicle_id, ts, voltage, rssi, queued, mode),
            )
            self._db.commit()

    def telemetry(self, vehicle_id: str, since: float, limit: int = 2000) -> list[dict[str, Any]]:
        with self._lock:
            rows = self._db.execute(
                "SELECT * FROM telemetry WHERE vehicle_id = ? AND ts >= ?"
                " ORDER BY ts ASC LIMIT ?",
                (vehicle_id, since, limit),
            ).fetchall()
        return [dict(r) for r in rows]

    # --- housekeeping -----------------------------------------------------

    def purge_old_positions(self, retain_days: int) -> int:
        """Drop raw positions older than the retention window. Trips stay."""
        if retain_days <= 0:
            return 0
        cutoff = time.time() - retain_days * 86400
        with self._lock:
            cur = self._db.execute("DELETE FROM positions WHERE ts < ?", (cutoff,))
            self._db.commit()
            deleted = cur.rowcount or 0
        if deleted:
            _LOG.info("purged %d positions older than %d days", deleted, retain_days)
        return deleted

    def stats(self) -> dict[str, Any]:
        with self._lock:
            def count(table: str) -> int:
                return int(self._db.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0])

            return {
                "vehicles": count("vehicles"),
                "positions": count("positions"),
                "trips": count("trips"),
                "events": count("events"),
                "telemetry": count("telemetry"),
            }
