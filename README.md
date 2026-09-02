# car-tracker

Tracker GPS/LTE do dwóch Mazd MX-5 (ND1 2016, ND3 2025), zasilany z gniazda OBD-II,
raportujący telemetrię do Home Assistant przez MQTT (EMQX, TLS).

Repo zawiera **całość**: założenia, analizę wariantów sprzętowych, schemat zasilania,
firmware ESP32 (PlatformIO, warstwa abstrakcji modemu, 4 warianty LTE + WiFi),
protokół MQTT i integrację HA (`custom_components/car_tracker`).

## Co jest w środku

| Katalog | Zawartość |
|---|---|
| `docs/` | PoC z posiadanego sprzętu (00), założenia, architektura, hardware, zasilanie z OBD, protokół, bezpieczeństwo, plan wdrożenia |
| `firmware/` | Projekt PlatformIO, 5 środowisk build (`wifi_dev`, `a7670e`, `sim7080g`, `sim7600e`, `lilygo_a7670`) |
| `ha-integration/` | Custom component `car_tracker` (device_tracker, sensory, przejazdy, geofence) |
| `hardware/` | Schematy połączeń, BOM per wariant, dane do obudowy |
| `tools/` | Skrypty pomocnicze (symulator telemetrii, provisioning MQTT/EMQX) |

## Start

**PoC robimy na sprzęcie, który już jest: ESP32 WiFi + NEO-6M, bez LTE, bez OBD.**
Żadnych zakupów przed zamknięciem PoC. Szczegóły i kryteria: `docs/00-poc.md`.

1. Przeczytaj `docs/00-poc.md` (co PoC sprawdza, czego nie sprawdzi, jak go uruchomić).
2. Przeczytaj `docs/01-zalozenia.md` (co robi, czego nie robi, budżet prądowy).
3. Zbuduj PoC: `firmware/` + `pio run -e wifi_dev -t upload`, transport przez WiFi.
4. Wgraj integrację: `ha-integration/custom_components/car_tracker` do `/config/custom_components/`.
5. Dopiero po PoC wybierz wariant sprzętowy: `docs/03-hardware-warianty.md`.
6. Zasilanie z OBD na samym końcu, po `docs/04-zasilanie-obd.md` (procedura pomiarowa, bezpieczniki).

## Stan

Faza PoC. Firmware kompiluje się we wszystkich pięciu środowiskach, PoC działa na
posiadanym ESP32 + NEO-6M przez WiFi. Sprzęt LTE nie kupiony i przed zamknięciem PoC
nie kupujemy.
Wszystko, co wymaga pomiaru na realnym aucie, jest w dokumentach oznaczone
`[DO ZMIERZENIA]` i nie jest podane jako fakt.

## Konwencje

- Kod, komentarze, commity, nazwy plików: angielski. Dokumentacja: polski.
- Sekrety (APN, hasło MQTT, PIN karty SIM) nigdy w repo: `firmware/src/config.h` jest w `.gitignore`,
  wzorzec w `config.example.h`, wartości w menedzer hasel.
- Remote: `git@git.example.lan:owner/car-tracker.git`.
