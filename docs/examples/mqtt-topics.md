# MQTT topics — ściągawka

Jedna strona z `docs/05`. Prefiks: `cartracker/<vehicle_id>/`.
Broker: TLS, użytkownik per pojazd (`docs/09` §9.2).

| Temat | Kierunek | QoS | Retained | Opis |
|---|---|---|---|---|
| `…/status` | urządzenie → | 1 | tak | `online` / `offline` (LWT) |
| `…/info` | urządzenie → | 1 | tak | fw, modem, IMEI, plate/VIN, IP panelu |
| `…/pos` | urządzenie → | 1 | **tak** | pozycja; odbiorca deduplikuje po `seq` |
| `…/tel` | urządzenie → | 1 | **tak** | napięcie, RSSI, kolejka `q`, tryb |
| `…/evt` | urządzenie → | 1 | nie | `trip_start`, `trip_end`, `motion_alarm`, … |
| `…/batch` | urządzenie → | 1 | nie | zaległości offline (max 50 pkt) |
| `…/cfg` | hub → urządzenie | 1 | tak | interwały, progi napięcia |
| `…/cmd` | HA/hub → urządzenie | 1 | **nie** | `ping`, `locate`, `reboot`, `ota`, `set_id` |
| `…/ack` | urządzenie → | 1 | nie | wynik komendy |
| `…/trip` | **hub** → HA | 1 | tak | dystans/czas/prędkości przejazdu |
| `homeassistant/…/config` | hub → HA | 1 | tak | MQTT discovery (21 encji / pojazd) |

**Uwagi szybkie**

- `pos`/`tel` retained od 2026-09-02: po restarcie HA mapa nie jest pusta, ale
  nie otwieraj przejazdu z retained pozycji (`docs/05` §5.1).
- `cmd` nigdy retained — retained `reboot` = pętla restartów.
- ACL: pojazd pisze tylko własny prefiks; HA pisze `cfg`/`cmd` — szkic w
  [`emqx-acl.md`](emqx-acl.md).
