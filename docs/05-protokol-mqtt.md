# 05. Protokół MQTT

Broker: EMQX, `mqtt.example.lan:8883`, TLS. Użytkownik dedykowany per pojazd, nie wspólny
`wspolne-konto`, żeby dało się odciąć jedno urządzenie bez ruszania reszty domu (patrz 09).

Prefiks tematów: `cartracker/<vehicle_id>/...`, gdzie `vehicle_id` to `nd1` albo `nd3`.
Identyfikator jest w NVS, nie w kompilacji, zgodnie z założeniem Z6.

## 5.1 Tabela tematów

| Temat | Kierunek | QoS | Retained | Opis |
|---|---|---|---|---|
| `cartracker/<id>/status` | urządzenie -> HA | 1 | tak | `online` / `offline`. `offline` jest ustawione jako LWT brokera |
| `cartracker/<id>/info` | urządzenie -> HA | 1 | tak | dane stałe: tożsamość pojazdu, wersja firmware, model modemu, IMEI, ICCID, MAC, adres panelu (5.9) |
| `cartracker/<id>/pos` | urządzenie -> HA | 1 | nie | pojedyncza pozycja |
| `cartracker/<id>/tel` | urządzenie -> HA | 1 | nie | telemetria bez pozycji (napięcie, zasięg, temperatura) |
| `cartracker/<id>/evt` | urządzenie -> HA | 1 | nie | zdarzenia: start jazdy, koniec jazdy, alarm ruchu, hibernacja |
| `cartracker/<id>/batch` | urządzenie -> HA | 1 | nie | paczka zaległych pozycji z kolejki offline |
| `cartracker/<id>/cfg` | HA -> urządzenie | 1 | tak | konfiguracja: interwały, progi napięcia, tryb |
| `cartracker/<id>/cmd` | HA -> urządzenie | 1 | nie | komendy jednorazowe: `ping`, `locate`, `reboot`, `ota` |
| `cartracker/<id>/ack` | urządzenie -> HA | 1 | nie | potwierdzenie komendy i wyniku |

`cfg` jest retained celowo: urządzenie po restarcie w garażu bez zasięgu i tak dostanie
ostatnią konfigurację przy pierwszym połączeniu, bez czekania na HA.

## 5.2 Pozycja (`pos`)

```json
{
  "seq": 10432,
  "ts": 1788345600,
  "ts_src": "gnss",
  "lat": 52.000000,
  "lon": 21.000000,
  "alt": 31.4,
  "spd": 64.2,
  "crs": 187,
  "sat": 9,
  "hdop": 0.9,
  "fix": 3,
  "st": "driving",
  "src": "neo6m"
}
```

| Pole | Typ | Znaczenie |
|---|---|---|
| `seq` | uint32 | licznik rosnący, w NVS, przetrwa restart. Służy do deduplikacji przy dosyłce |
| `ts` | uint32 | UTC unix. `0` oznacza brak wiarygodnego czasu |
| `ts_src` | enum | `gnss`, `ntp` albo `none`, patrz 2.7 |
| `spd` | float | km/h |
| `crs` | uint16 | kurs w stopniach, 0-359 |
| `hdop` | float | rozmycie poziome, integracja odrzuca punkty powyżej progu |
| `fix` | uint8 | 0 brak, 2 fix 2D, 3 fix 3D |
| `st` | enum | `driving`, `parked`, `moved`, `hibernate` |
| `src` | enum | `neo6m` albo `modem`, żeby dało się porównać oba odbiorniki |

Pole `src` jest w protokole od początku, bo w wariantach A, C i D mamy dwa niezależne
odbiorniki GNSS i chcemy widzieć w HA, który dał punkt. To także sposób na wykrycie
awarii jednego z nich bez wyjmowania urządzenia z auta.

## 5.3 Telemetria (`tel`)

```json
{
  "seq": 10433,
  "ts": 1788345660,
  "vbat": 12.42,
  "vsys": 3.31,
  "temp": 21.8,
  "rssi": -71,
  "net": "LTE",
  "op": "26006",
  "roam": false,
  "up": 84213,
  "q": 0,
  "rst": 3,
  "st": "parked"
}
```

`q` to liczba punktów zalegających w kolejce offline. Wystawiona jako sensor w HA jest
najprostszym wskaźnikiem, że coś jest nie tak z łącznością, zanim urządzenie zamilknie.
`rst` to powód ostatniego restartu z API ESP32, co odróżnia reset od brownoutu.

## 5.4 Zdarzenia (`evt`)

```json
{ "seq": 10434, "ts": 1788345700, "ev": "trip_start", "lat": 53.42, "lon": 14.55 }
```

Wartości `ev`: `trip_start`, `trip_end`, `motion_alarm`, `battery_low`, `hibernate`,
`wakeup`, `power_lost`, `power_restored`, `gnss_lost`, `gnss_ok`.

`power_lost` jest wysyłane z energii zgromadzonej w kondensatorze po odpięciu wtyku OBD,
o ile modem jest w tym momencie zarejestrowany. Jeżeli nie zdąży, HA i tak zobaczy
`offline` z LWT (kryterium akceptacji 6 z rozdziału 01).

## 5.5 Paczka zaległości (`batch`)

```json
{ "n": 3, "pts": [ {...pos...}, {...pos...}, {...pos...} ] }
```

Maksymalnie 50 punktów w paczce, wysyłane po odzyskaniu łącza, od najstarszego.
Rekord jest kasowany z kolejki dopiero po PUBACK. Integracja odrzuca punkty o `seq`
już widzianym, co czyni całą operację idempotentną.

## 5.6 Konfiguracja (`cfg`)

```json
{
  "int_drive": 30,
  "int_park": 3600,
  "int_alarm": 15,
  "v_drive_on": 13.2,
  "v_drive_off": 13.0,
  "v_warn": 12.2,
  "v_hib": 11.9,
  "v_wake": 12.4,
  "crs_delta": 25,
  "hdop_max": 3.0,
  "motion_sens": 3,
  "gnss_src": "auto"
}
```

Wszystkie progi z rozdziałów 02 i 04 są tutaj, żeby dało się je poprawić po pomiarach
na aucie bez podłączania kabla. `gnss_src` przyjmuje `neo6m`, `modem` albo `auto`,
gdzie `auto` bierze pierwszy fix z lepszym HDOP.

## 5.7 Komendy (`cmd`)

```json
{ "id": "a1b2", "cmd": "locate" }
```

| Komenda | Działanie |
|---|---|
| `ping` | odpowiedź na `ack`, mierzy czas obiegu |
| `locate` | wybudza GNSS i modem, wysyła jedną pozycję niezależnie od stanu |
| `reboot` | restart urządzenia |
| `ota` | pobranie firmware, dozwolone tylko po WiFi, patrz 07 |
| `set_id` | zmiana `vehicle_id` w NVS, wymaga potwierdzenia w polu `confirm` |

Odpowiedź na `cartracker/<id>/ack`:
```json
{ "id": "a1b2", "ok": true, "ms": 812, "msg": "" }
```

Komendy nie mają retained. Retained komenda odtwarzałaby się przy każdym połączeniu,
co przy `reboot` daje pętlę restartów. To jest częsty błąd w tego typu integracjach
i dlatego jest tu zapisany wprost.

## 5.8 Rozmiar pakietu i zużycie danych

Pakiet `pos` w powyższej postaci to około 200 bajtów JSON. Przy 30 s interwale to
24 kB na godzinę jazdy, czyli więcej niż zakładany budżet z 1.6. Dlatego firmware ma
tryb `compact`, w którym klucze są skrócone do dwóch znaków, a pola opcjonalne
pomijane przy braku zmiany. Wtedy pakiet schodzi poniżej 120 bajtów.

JSON zostaje mimo wszystko zamiast formatu binarnego, bo cała reszta domu mówi JSON-em
przez MQTT i debugowanie binarnego protokołu w aucie w trasie kosztuje więcej niż
oszczędność transferu, która i tak mieści się w pakiecie danych.

## 5.9 Dane stałe (`info`)

```json
{
  "name": "MX-5 ND1",
  "plate": "ZS12345",
  "vin": "JM1NDAM75M0300001",
  "fw": "0.1.0",
  "modem": "none-wifi",
  "imei": "AA:BB:CC:DD:EE:FF",
  "iccid": "",
  "net": "WIFI",
  "ip": "192.0.2.42"
}
```

Retained, publikowane raz po połączeniu z brokerem.

`name`, `plate` i `vin` to tożsamość auta, w którym siedzi płytka. Trzymamy ją
w NVS urządzenia, a nie w agregatorze, bo wtedy jest jedno miejsce do zmiany
i strona floty podpisuje auto sama, bez drugiego rejestru do pilnowania.
`plate` i `vin` bywają puste — płytka na biurku nie należy jeszcze do żadnego auta.
Firmware odrzuca VIN o długości innej niż 17 znaków oraz taki z literami I, O, Q
(ISO 3779), więc pusty znaczy „nie podano", a nie „nie udało się wpisać".

`ip` pozwala agregatorowi proxować panel urządzenia pod `/device/<id>/`.
Bez niego hub musiałby zgadywać adres przydzielony z DHCP.

Agregator nadpisuje tylko te pola, które w wiadomości faktycznie są. Starsze
firmware, które nie zna `plate` ani `vin`, nie wyczyści wartości zgłoszonej
wcześniej przez nowsze.
