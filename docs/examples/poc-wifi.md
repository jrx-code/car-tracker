# PoC `wifi_dev` — szybki start

Skrót z `docs/00` §0.5. Cel: bench / hotspot w aucie, bez modemu i bez zasilania
z OBD.

## 1. Konfiguracja fabryczna

```bash
cd firmware
cp src/config.example.h src/config.h
```

`config.h` jest w `.gitignore`. Wypełnij z menedżera haseł (nie commituj):

| Pole | Skąd |
|---|---|
| `WIFI_SSID` / `WIFI_PASS` | hotspot telefonu albo Wi‑Fi w garażu |
| `MQTT_HOST` / `MQTT_PORT` | broker (np. `mqtt.example.lan:8883`) |
| `MQTT_USER` / `MQTT_PASS` | konto per pojazd, np. `cartracker-nd1` |
| `MQTT_ROOT_CA` | CA z łańcucha brokera — **nie** ogólny root tej samej marki; wyciągnięcie w `docs/13` §13.5 |

Pola LTE (`GSM_*`) na `wifi_dev` nie są używane.

## 2. Build i wgranie

```bash
pio run -e wifi_dev -t upload
pio device monitor
```

To samo środowisko co w weryfikacji z `CLAUDE.md`. Po wgraniu ustawienia robocze
możesz dogrywać w portalu (`docs/13`); `config.h` to tylko fabryka.

## 3. HA i symulator (bez sprzętu)

HA: integracja MQTT na tego samego brokera. Encje powstają z discovery huba
(`docs/08`). Przed jazdą warto przepuścić symulator:

```bash
export MQTT_PASS='…'   # z menedżera haseł
tools/sim_track.py --vehicle nd1 --trip --fast
tools/sim_track.py --vehicle nd1 --backlog 120 --duplicate

# bez brokera — tylko podgląd payloadów:
tools/sim_track.py --vehicle nd1 --trip --dry-run
```

## 4. Co PoC ma zamknąć

Kryteria w `docs/00` §0.6 (ciągły ślad, kolejka offline, liczby z NEO-6M,
interwał/kurs). Dopiero potem decyzje zakupowe z `docs/03`.
