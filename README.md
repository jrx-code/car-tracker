# car-tracker

Tracker GPS/LTE do dwóch Mazd MX-5 (ND1 2016, ND3 2025), zasilany z gniazda OBD-II,
raportujący telemetrię do Home Assistant przez MQTT (EMQX, TLS).

Repo zawiera **całość**: założenia, analizę wariantów sprzętowych, schemat zasilania,
firmware ESP32 (PlatformIO, warstwa abstrakcji modemu, 4 warianty LTE + WiFi),
protokół MQTT i integrację HA (`custom_components/car_tracker`).

## Co jest w środku

| Katalog | Zawartość |
|---|---|
| `docs/` | Założenia, architektura, hardware, zasilanie z OBD, protokół, bezpieczeństwo, plan wdrożenia |
| `firmware/` | Projekt PlatformIO, 5 środowisk build (`wifi_dev`, `a7670e`, `sim7080g`, `sim7600e`, `lilygo_a7670`) |
| `ha-integration/` | Custom component `car_tracker` (device_tracker, sensory, przejazdy, geofence) |
| `hardware/` | Schematy połączeń, BOM per wariant, dane do obudowy |
| `tools/` | Skrypty pomocnicze (symulator telemetrii, provisioning MQTT/EMQX) |

## Start

1. Przeczytaj `docs/01-zalozenia.md` (co robi, czego nie robi, budżet prądowy).
2. Wybierz wariant sprzętowy: `docs/03-hardware-warianty.md` (porównanie 4 modemów + BOM).
3. Zbuduj bench na WiFi: `firmware/` + `pio run -e wifi_dev -t upload`.
4. Wgraj integrację: `ha-integration/custom_components/car_tracker` do `/config/custom_components/`.
5. Zasilanie z OBD dopiero po `docs/04-zasilanie-obd.md` (procedura pomiarowa, bezpieczniki).

## Stan

Faza 0 (projekt i firmware bench na WiFi) gotowa do budowy. Sprzęt LTE nie kupiony.
Wszystko, co wymaga pomiaru na realnym aucie, jest w dokumentach oznaczone
`[DO ZMIERZENIA]` i nie jest podane jako fakt.

## Konwencje

- Kod, komentarze, commity, nazwy plików: angielski. Dokumentacja: polski.
- Sekrety (APN, hasło MQTT, PIN karty SIM) nigdy w repo: `firmware/src/config.h` jest w `.gitignore`,
  wzorzec w `config.example.h`, wartości w menedzer hasel.
- Remote: `git@git.example.lan:owner/car-tracker.git`.
