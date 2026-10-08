# tracker-hub

Aggregator and web UI for the fleet of `car-tracker` devices. One service
collects data from **every** tracker over MQTT, keeps its own history and serves
a map and an API. It is built for more than one vehicle from the start: ND1 and
ND3 are the first two, not the only ones planned.

It runs natively under systemd in an LXC behind a reverse proxy. No Docker: it
is a single Python process.

```
tracker nd1 ─┐
tracker nd3 ─┼─ MQTT/TLS ─> broker ─> tracker-hub (LXC) ─> map, trips, API
tracker ...  ─┘                          │
                                         └─> Home Assistant (separately, through its own integration)
```

## Why a separate service when there is a HA integration

The HA integration provides entities for automations, and that is its job. The
hub covers what an entity cannot:

- **History independent of HA.** A restart, upgrade or failure of HA does not
  lose the track. The database lives here.
- **Fleet map.** All trackers on one map, the track of a trip one click away from
  the list. In HA that would be a dashboard to maintain by hand.
- **Trips as objects.** Distance, duration, top and average speed, computed once
  and stored, not recomputed on every read.
- **One place for many vehicles.** Adding a third tracker means publishing to a
  new topic, with no change to the hub's configuration.

HA and the hub read the same topics independently. Neither depends on the other.

## What it does with the data

Protocol: `../docs/05-protokol-mqtt.md`. The hub subscribes to
`cartracker/+/#` and accepts both payload variants (full and compact). The
firmware side of the same contract is `../firmware/src/telemetry/packet.cpp`.

Things that are easy to get wrong and are handled here:

| Problem | Solution |
|---|---|
| Replaying the offline queue produces duplicates by design (a record leaves the device queue only after PUBACK) | unique index on `(vehicle_id, seq)`, duplicates are dropped silently |
| A GNSS glitch can throw a point hundreds of km away | a jump that needs more than 300 km/h is rejected |
| A weak fix spoils the track | points with HDOP worse than 5.0 are rejected |
| `trip_start` can be lost in a coverage gap | a `driving` position without an open trip opens one |
| A lost `trip_end` would leave a trip open forever | a new `trip_start` closes the previous one; never two open trips per vehicle |
| The raw track reproduces daily routes to the minute | positions expire after `RETAIN_POSITIONS_DAYS` (default 180), trips stay |

## Interface

- **Overview:** one row per car. Data on the left (mode, voltage, speed,
  satellites, HDOP, signal, time since last contact, coordinates), a thumbnail
  map with the current position on the right, with **Enlarge** below it. Each
  vehicle has a fixed colour, in the row and on the map.
- **Registration and VIN** in the row header, if the device reported them in
  `info` (set in the device portal, not here). Empty fields take no space.
- **Large map** opens as a full-window overlay: all trackers plus the last 24 h
  of the selected one. Escape closes it.
- **Vehicle details:** trip list (a click opens the route on the large map),
  events, a battery voltage chart for the last two weeks.
- **Commands:** `locate` and `ping` straight from the page.

Warnings in a row are ordered by what really needs attention: movement with the
engine off, offline, long silence, a growing offline queue (the earliest sign
that the link is failing), low voltage.

### Admin mode

The **Admin mode** button in the header goes through an Authentik sign-in
(`/admin`) and returns to the page with the admin controls unlocked: the
**Settings** tab in vehicle details, command buttons and the device panel.
Without signing in the page is read only, and the hub refuses every write no
matter what the page shows.

The **Settings** tab:

- **Connectivity and identity**: modem, IMEI, ICCID, network, operator, signal,
  portal address.
- **Device settings** (SIM and APN, vehicle, WiFi, MQTT, portal and access
  point, pins): read live from the board's portal. Works only while the hub can
  reach the tracker on the local network; a car on LTE has a carrier address.
  Only changed fields are sent, an empty password means "unchanged". Network,
  broker, TLS and pins take effect after a restart, hence the restart button
  after saving.
- **Operating parameters** (intervals, voltage thresholds, HDOP, motion
  sensitivity): retained `cfg` topic over MQTT, so they reach a car on the road
  too. Changing one field is merged into the last set instead of replacing it.
- **Commands**: locate, ping, restart, access point, with a list of recent
  replies (`ack`).

The hub trusts `X-Authentik-Username` only from `TRUSTED_PROXIES` and only for
users on `ADMIN_USERS`. The reverse proxy must also strip that header from
client requests. Every write (commands, `cfg`, device settings) needs admin mode
or `API_TOKEN`; an empty token does **not** open writes.

## API

| Method | Path | Description |
|---|---|---|
| GET | `/api/health` | MQTT state, database counters, configuration without secrets |
| GET | `/api/vehicles` | all vehicles with their last position and open trip |
| GET | `/api/vehicles/<id>` | vehicle + last 20 trips and events |
| GET | `/api/vehicles/<id>/positions?hours=24` or `?trip=<id>` | track |
| GET | `/api/vehicles/<id>/trips` | trips |
| GET | `/api/vehicles/<id>/events` | events |
| GET | `/api/vehicles/<id>/telemetry?hours=168` | voltage, signal, queue |
| GET | `/api/trips/<id>` | trip with its full track |
| POST | `/api/vehicles/<id>/command` | `{"cmd": "locate"\|"ping"\|"reboot"}` |
| POST | `/api/vehicles/<id>/config` | retained `cfg` to the device |

POSTs need admin mode or the `X-Api-Token` header matching `API_TOKEN`.

## Installation

```bash
# 1. container (once), on a Proxmox VE host reachable as "pve"
CTID=<id> IP=<addr/prefix> GW=<gateway> ./provision.sh            # dry run
CTID=<id> IP=<addr/prefix> GW=<gateway> APPLY=1 ./provision.sh    # create it

# 2. MQTT user for the hub on the broker, ACL limited to cartracker/#

# 3. code and service (every time)
TARGET=root@<lxc-address> ./deploy.sh

# 4. reverse proxy and DNS
```

Reverse proxy, Caddy example:

```caddyfile
tracker.example.com {
	reverse_proxy <lxc-address>:8080
}
```

The overview page shows where the cars are right now. Keep it off the public
internet (split-horizon DNS, LAN only) or put the whole site behind
authentication, not just `/admin`.

## Configuration

Everything through the environment, file `/etc/tracker-hub/tracker-hub.env`
(systemd `EnvironmentFile`, mode 640, owner `root:tracker`). Template:
`.env.example`. Secrets come from a password manager, never from the repo.

## Development and tests

```bash
python3 -m pytest tests/ -q          # no broker and no network needed
python3 tools_seed.py /tmp/demo.db   # two example vehicles
DB_PATH=/tmp/demo.db MQTT_PASS=x MQTT_HOST=127.0.0.1 MQTT_TLS=false \
  HTTP_PORT=8099 python3 -m tracker_hub
```

The tests cover what actually breaks: deduplication by `seq`, rejecting
impossible jumps and bad HDOP, trip distance, a trip opened without
`trip_start`, an offline queue replayed twice, two vehicles with the same `seq`,
garbage payloads, HA discovery and admin mode.

`tests/conftest.py` blocks paho's optional `import dns.resolver`. The reason is
in the comment there; it affects some workstations only, not the container.

## Dependencies

`python3` and `python3-paho-mqtt` from apt. Nothing from pip, no venv. Leaflet is
vendored at deploy time into `web/vendor/`, so the page works without a CDN.
