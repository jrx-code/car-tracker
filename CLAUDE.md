# car-tracker

Tracker GPS/LTE do dwóch Mazd MX-5 ND (ND1 2016, ND3 2025), zasilany z OBD-II,
raportujący do Home Assistant przez MQTT.

## Struktura

- `docs/` — PoC na posiadanym sprzęcie (00), założenia (01), architektura (02), warianty sprzętu (03), zasilanie (04),
  protokół MQTT (05), CAN jako faza 2 (06), firmware (07), integracja HA (08),
  bezpieczeństwo (09), różnice między autami (10), plan wdrożenia (11)
- `firmware/` — PlatformIO, ESP32, 5 środowisk (wifi_dev + 4 warianty modemu)
- `firmware/probe/` — osobny mini projekt do bring-upu GPS (skan baud rate, diagnostyka NMEA)
- `ha-integration/custom_components/car_tracker/` — integracja HA
- `tools/sim_track.py` — symulator telemetrii, testy bez sprzętu

## Zasady w tym repo

- **PoC budujemy wyłącznie z posiadanego sprzętu: ESP32 WiFi + NEO-6M, transport przez
  WiFi (`pio run -e wifi_dev`), zasilanie z USB, nie z OBD.** Nic nie kupujemy przed
  zamknięciem PoC wg kryteriów z `docs/00-poc.md`. Nie proponować zakupów wcześniej.
- Dokumentacja po polsku, kod i commity po angielsku.
- `firmware/src/config.h` jest w `.gitignore`. Sekrety z menedzer hasel, nigdy w repo.
- **Wszystko, czego nie zmierzono na aucie, jest oznaczone `[DO ZMIERZENIA]`.**
  Nie zamieniać takich znaczników na twierdzenia bez pomiaru.
- Zmiana formatu `PosRecord` w `state.h` wymaga podbicia `kStoreVersion`
  w `telemetry/store.cpp`, inaczej stara kolejka zostanie odczytana jako śmieci.
- Zmiana pakietu w `telemetry/packet.cpp` wymaga zmiany parsera w
  `ha-integration/.../coordinator.py`. Format jest w `docs/05`.
- Nic nie jest wysyłane na magistralę CAN auta. Faza 2 jest pasywna, warunki w `docs/06`.
- **`config.h` to ustawienia fabryczne, nie konfiguracja.** Wartości robocze są w NVS
  i edytuje się je w portalu (`docs/13`). Nowe ustawienie dodaje się w
  `settings.h/.cpp` i w `portal_page.h`, nie przez kolejny `#define`.
- **Certyfikat CA bierz z łańcucha, który wysyła serwer**, nie z ogólnego roota tej
  samej marki. Broker kotwiczy w ISRG Root YR; z X1 (cross-signer) BearSSL nie
  zbuduje ścieżki i TLS pada. Szczegóły w `docs/13` punkt 13.5.
- **Nigdy `pinMode()` na pinie przypisanym do UART.** `pinMode()` woła
  `perimanClearPinBus()` i odbiera pin peryferium; odbiornik milczy, a każde API
  udaje sukces. Podciągnięcie przez `gpio_set_pull_mode()`. Po `pinMode()` na takim
  pinie konieczny pełny `end()` + `begin()`, samo `setPins()` nie wystarczy.
  Kosztowało to kilka godzin 2026-09-02, opis w `docs/12-bring-up.md` punkt 12.8.
- **Nie wołać `HardwareSerial::end()` żeby zmienić prędkość UART.** W Arduino core 3.3.9
  `end()` odpina piny i usuwa sterownik; cykliczne begin/end na UART1 zalewa konsolę USB
  powtarzającym się tekstem (1,8 MB w 20 s, zdiagnozowane 2026-09-02). Od zmiany prędkości
  jest `updateBaudRate()`. Szczegóły w `docs/12-bring-up.md` punkt 12.4.
- Bluetooth nie jest transportem: ESP-IDF nie ma PAN/BNEP, a ten core nie ma nawet
  bibliotek BT. Uzasadnienie w `docs/12-bring-up.md` punkt 12.2, nie rozważać od nowa.

## Weryfikacja przed commitem

```bash
cd firmware && pio run -e wifi_dev -e sim7670g -e a7670e -e sim7080g -e sim7600e -e lilygo_a7670
ruff check ha-integration/custom_components/car_tracker tools/
```

## Stan

Faza PoC. **Bench działa**: NEO-6M ma fix (6 satelitów, HDOP 1,6, TTFF 151 s
w budynku), dane lecą przez WiFi na `http://gps-probe.local/`. Liczby w
`hardware/pomiary.md`, przebieg w `docs/12-bring-up.md`.

Agregator floty stoi osobno: `tracker-hub`, LXC 420, https://tracker.example.lan
(repo `z4-server/serwisy/tracker-hub`).

Kolejny krok: wynieść zestaw na zewnątrz i do auta (`docs/00-poc.md` 0.6),
równolegle pomiary W1-W3 z `docs/11` (pobór spoczynkowy aut, napięcie na pinie 16).
