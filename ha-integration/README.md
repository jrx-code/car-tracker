# custom_components/car_tracker

> **Uwaga: ta integracja jest zastąpiona.** Encje w Home Assistant tworzy dziś
> MQTT discovery publikowane przez `tracker-hub`, a ten katalog zostaje w repo
> tylko jako referencja. Nie instaluj jej równolegle z discovery, bo obie
> utworzyłyby te same encje. Uzasadnienie i stan:
> [`docs/08-ha-integracja.md` §8.3](../docs/08-ha-integracja.md#83-mqtt-discovery-czy-własna-integracja).
> Poniższa instrukcja instalacji opisuje stary wariant.

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
