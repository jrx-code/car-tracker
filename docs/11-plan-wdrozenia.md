# 11. Plan wdrożenia

Kolejność jest tak dobrana, żeby najdroższe pomyłki (rozładowany akumulator, uszkodzona
magistrala, spalona wiązka) były niemożliwe do popełnienia na wczesnym etapie.

## Faza PoC: posiadany sprzęt, zero zakupów

Pełny opis w `docs/00-poc.md`. W skrócie: ESP32 WiFi + NEO-6M, transport przez WiFi
(hotspot telefonu w aucie), zasilanie z USB. PoC ma odpowiedzieć na pytanie, czy
pomysł działa end to end i czy NEO-6M wystarcza, **zanim cokolwiek kupimy**.
Kroki B1-B5 poniżej są równocześnie krokami PoC, bo używają tego samego środowiska
`wifi_dev`. Różnica jest taka, że PoC wychodzi z nimi do auta, a nie kończy na stole.

## Faza 0: bench na WiFi (bez zakupów)

| Krok | Co | Kryterium zamknięcia |
|---|---|---|
| B1 | ESP32 + NEO-6M na stole, `pio run -e wifi_dev -t upload` | Fix z NEO-6M na parapecie, NMEA parsowane |
| B2 | Połączenie z EMQX po TLS, publikacja `pos` i `tel` | Widoczne w `mosquitto_sub` i w logu brokera |
| B3 | Integracja HA, encje, mapa | `device_tracker.nd1` porusza się przy symulacji z `tools/sim_track.py` |
| B4 | Kolejka offline: odcięcie WiFi na 10 min w trakcie symulacji | Po powrocie punkty dochodzą z oryginalnym `ts`, bez luk i bez duplikatów |
| B5 | Deep sleep i wake z akcelerometru | Wybudzenie na ruch działa, czas wybudzenia poniżej 2 s |

Faza 0 nie wymaga kupowania niczego poza akcelerometrem i domyka całą logikę aplikacji.

## Faza 1: pomiary na autach (bez montażu)

| Krok | Co | Uwaga |
|---|---|---|
| W1 | Pomiar poboru spoczynkowego **przed** montażem, oba auta | Punkt odniesienia, patrz 4.6. Bez tego nie da się później rozstrzygnąć, czy tracker szkodzi |
| W2 | Pomiar napięcia na pinie 16 i 4: postój, zapłon, praca silnika, i-stop | Z tego wynikają progi z 5.6 |
| W3 | Sprawdzenie, czy pin 16 gaśnie po zaśnięciu auta | Jeśli gaśnie, zmienia się koncepcja PARKED |
| W4 | Kalibracja dwupunktowa ADC dzielnika napięcia | Zapis do NVS, patrz 4.4 |
| W5 | Test miejsca dla anteny GNSS i LTE | Podszybie, nie kokpit. Sprawdzić z zamkniętym dachem i z otwartym |

Bez zamkniętych W1-W3 nie wpinamy urządzenia na stałe.

## Faza 2: zakup i budowa

**Warunek wejścia: zamknięty PoC wg kryteriów z `docs/00-poc.md` punkt 0.6.**
Wynik PoC zmienia listę zakupów: jeżeli NEO-6M gubi fix w mieście, kupujemy też
odbiornik wielosystemowy, a nie sam modem.

| Krok | Co |
|---|---|
| Z1 | Decyzja o wariancie z rozdziału 03 na podstawie wyniku PoC, sprawdzonej dostępności LTE-M u operatora i ceny |
| Z2 | Zakup modemu, przetwornicy, elementów ochronnych, wtyku OBD, anten |
| Z3 | Montaż toru zasilania **bez modemu i bez GNSS**, pomiar poboru samego ESP32 w deep sleep na zasilaczu laboratoryjnym 12,6 V |
| Z4 | Kryterium: poniżej 2 mA. Jeżeli nie, wracamy do 4.2 i eliminujemy źródło, nie idziemy dalej |
| Z5 | Dołożenie modemu i GNSS z load switchami, powtórzenie pomiaru |
| Z6 | Test 72 h na zasilaczu laboratoryjnym z licznikiem amperogodzin |

## Faza 3: montaż w ND1 (auto starsze i tańsze, mniejsze ryzyko)

| Krok | Co |
|---|---|
| M1 | Wpięcie do OBD, obserwacja przez 7 dni w HA: napięcie, kolejka, zasięg |
| M2 | Pomiar poboru spoczynkowego auta z trackerem i porównanie z W1 |
| M3 | Przejazd testowy 20 km, weryfikacja kryterium akceptacji 3 z rozdziału 01 |
| M4 | Test alarmu: przepchnięcie auta o kilka metrów przy wyłączonym silniku |
| M5 | Test hibernacji przez obniżenie napięcia zasilaczem laboratoryjnym na stole, nie w aucie |

## Faza 4: montaż w ND3

Dopiero po 30 dniach bezawaryjnej pracy w ND1. Ten sam zestaw kroków M1-M5.

## Faza 5: rozwój

- Odczyt CAN, najpierw ND1, kroki K1-K6 i warunki wejścia w rozdziale 06.
  Nasłuch pasywny przez cały czas, zapytania OBD wyłącznie przy pracującym
  silniku, transceiver odcięty load switchem na postoju.
- Wymiana NEO-6M na odbiornik wielosystemowy, jeśli pomiary z pola pokażą, że fix
  gubi się w mieście. Decyzja z danych, nie z założenia.
- Automatyzacje HA: powiadomienie o niskim napięciu ND1, geofence garaż,
  raport miesięczny przejazdów.
