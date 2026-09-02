# car-tracker

Tracker GPS/LTE do dwóch Mazd MX-5 ND (ND1 2016, ND3 2025), zasilany z OBD-II,
raportujący do Home Assistant przez MQTT.

## Struktura

- `docs/` — założenia (01), architektura (02), warianty sprzętu (03), zasilanie (04),
  protokół MQTT (05), CAN jako faza 2 (06), firmware (07), integracja HA (08),
  bezpieczeństwo (09), różnice między autami (10), plan wdrożenia (11)
- `firmware/` — PlatformIO, ESP32, 5 środowisk (wifi_dev + 4 warianty modemu)
- `ha-integration/custom_components/car_tracker/` — integracja HA
- `tools/sim_track.py` — symulator telemetrii, testy bez sprzętu

## Zasady w tym repo

- Dokumentacja po polsku, kod i commity po angielsku.
- `firmware/src/config.h` jest w `.gitignore`. Sekrety z menedzer hasel, nigdy w repo.
- **Wszystko, czego nie zmierzono na aucie, jest oznaczone `[DO ZMIERZENIA]`.**
  Nie zamieniać takich znaczników na twierdzenia bez pomiaru.
- Zmiana formatu `PosRecord` w `state.h` wymaga podbicia `kStoreVersion`
  w `telemetry/store.cpp`, inaczej stara kolejka zostanie odczytana jako śmieci.
- Zmiana pakietu w `telemetry/packet.cpp` wymaga zmiany parsera w
  `ha-integration/.../coordinator.py`. Format jest w `docs/05`.
- Nic nie jest wysyłane na magistralę CAN auta. Faza 2 jest pasywna, warunki w `docs/06`.

## Weryfikacja przed commitem

```bash
cd firmware && pio run -e wifi_dev -e sim7670g -e a7670e -e sim7080g -e sim7600e -e lilygo_a7670
ruff check ha-integration/custom_components/car_tracker tools/
```

## Stan

Faza 0 (projekt i firmware bench). Sprzęt LTE nie kupiony, nic nie zamontowane
w aucie. Kolejny krok to kroki W1-W3 z `docs/11-plan-wdrozenia.md`: pomiary na autach.
