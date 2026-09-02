# custom_components/car_tracker

Integracja Home Assistant dla trackera z tego repo. Wymaga skonfigurowanej
integracji MQTT w HA (broker EMQX, `mqtt.example.lan:8883`).

## Instalacja

Skopiuj `custom_components/car_tracker` do `/config/custom_components/`,
zrestartuj HA, dodaj integrację przez UI. Jeden wpis konfiguracyjny na pojazd,
`vehicle_id` musi się zgadzać z identyfikatorem w NVS urządzenia.

Szczegóły, encje, automatyzacje i sposób testowania bez sprzętu:
`../docs/08-ha-integracja.md`.

## Zależności

Tylko `mqtt` z rdzenia HA. Brak zależności zewnętrznych (`requirements` puste).
