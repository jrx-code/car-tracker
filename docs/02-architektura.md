# 02. Architektura

## 2.1 Rzut z lotu ptaka

```
    AUTO (ND1 / ND3)
    +--------------------------------------------------+
    |  OBD-II J1962                                     |
    |   pin 16 +12V stale   pin 4/5 GND                 |
    |        |                   |                      |
    |   [bezpiecznik 500 mA] [TVS 24V] [odwrotna polaryzacja]
    |        |                                          |
    |   [buck 12V -> 3.8V, niski Iq]                    |
    |        |                                          |
    |   +----+-------------------------+                |
    |   |          ESP32               |                |
    |   |  - maszyna stanow            |                |
    |   |  - kolejka offline (NVS/LittleFS)             |
    |   |  - filtr pozycji             |                |
    |   +--+--------+--------+---------+                |
    |      |        |        |                          |
    |   [GNSS]  [MODEM]  [MPU6050/LIS3DH]               |
    |   NEO-6M   LTE      akcelerometr, przerwanie wake |
    |      |        |                                   |
    |    antena   antena LTE + SIM                      |
    +--------------------------------------------------+
                     |
                 LTE / WiFi
                     |
              mqtt.example.lan:8883 (EMQX, TLS)
                     |
        +------------+-------------+
        |                          |
  Home Assistant             InfluxDB (retencja historii)
  custom_components/car_tracker
        |
  device_tracker + sensory + przejazdy + geofence
```

## 2.2 Warstwy firmware

Firmware jest podzielony tak, żeby zmiana modemu nie dotykała logiki aplikacji.
To wynika z Z6 i z tego, że w chwili pisania nie mamy jeszcze kupionego modemu.

| Warstwa | Pliki | Odpowiedzialność |
|---|---|---|
| Aplikacja | `src/main.cpp`, `src/telemetry/` | maszyna stanów, decyzja kiedy nadawać, budowa pakietu |
| Transport | `src/modem/ITransport.h` + implementacje | abstrakcja "wyślij bajty na temat MQTT", nie wie czy pod spodem LTE czy WiFi |
| Modem | `src/modem/*.cpp` | AT, PDP, rejestracja w sieci, sygnał, sen modemu |
| GNSS | `src/gnss/` | NMEA, filtr jakości fixa, wybór źródła (zewnętrzny NEO-6M albo GNSS modemu) |
| Zasilanie | `src/power/` | pomiar napięcia, progi, deep sleep, load switche, decyzja o hibernacji |
| Trwałość | `src/telemetry/store.*` | kolejka FIFO punktów we flashu, dosyłka po odzyskaniu łącza |

Kluczowa zasada: **jedno źródło prawdy o stanie** trzyma `VehicleState` w `main.cpp`.
Moduły niższych warstw raportują fakty (napięcie, fix, ruch, zasięg) i nigdy same nie
decydują o wysyłce ani o śnie.

## 2.3 Maszyna stanów

```
                     napiecie > 13,2 V przez 5 s
      +--------+     albo ruch z akcelerometru      +---------+
      | PARKED | ---------------------------------> | DRIVING |
      +--------+                                    +---------+
        ^   ^                                          |    |
        |   |     napiecie < 13,0 V przez 120 s        |    |
        |   +------------------------------------------+    |
        |                    i brak ruchu                    |
        |                                                    |
        |  napiecie > 12,4 V                                 |
   +-----------+                                             |
   | HIBERNATE | <-- napiecie < 11,9 V przez 10 min ---------+
   +-----------+
        |
        +-- budzenie co 6 h tylko po to, zeby zmierzyc napiecie i ewentualnie wysrac
            jeden pakiet alarmowy o stanie akumulatora
```

Dodatkowy stan poprzeczny: **MOVED_WHILE_PARKED**. Jeżeli w `PARKED` akcelerometr
zgłasza ruch trwający ponad 10 s, a napięcie nie wzrosło (silnik nie pracuje),
tracker natychmiast wybudza modem i nadaje alarm. To przypadek holowania albo lawety.

Progi napięciowe są **konfigurowalne z HA**, nie zaszyte, bo alternator MX-5
`[DO ZMIERZENIA: rzeczywisty profil napięcia przy pracy silnika, ND ma układ ładowania
o zmiennym napięciu i wersje z i-stop, więc progi domyślne mogą wymagać korekty]`.

## 2.4 Dlaczego dwa źródła detekcji jazdy

Detekcja z samego napięcia zawodzi: przy i-stop na światłach napięcie spada do poziomu
postoju, a przy ładowaniu regeneracyjnym potrafi skakać. Detekcja z samego akcelerometru
zawodzi przy myjni, holowaniu na pasach i przy trzaskaniu drzwiami.

Decyzja: jazda = (napięcie powyżej progu) **lub** (ruch trwały ponad 10 s).
Postój = (napięcie poniżej progu) **i** (brak ruchu) przez 120 s. Histereza po obu stronach.

## 2.5 Rytm nadawania

| Stan | Interwał pozycji | Interwał telemetrii |
|---|---|---|
| DRIVING, prędkość powyżej 5 km/h | 30 s, dodatkowo przy zmianie kursu ponad 25 stopni | 60 s |
| DRIVING, postój na światłach | 120 s | 120 s |
| PARKED | brak pozycji, GNSS wyłączony | 1 h (napięcie, temperatura, zasięg) |
| MOVED_WHILE_PARKED | 15 s | 15 s |
| HIBERNATE | brak | 6 h, jeden pakiet |

Zmiana kursu jako dodatkowy wyzwalacz daje wierny ślad na zakrętach bez zwiększania
liczby punktów na prostej. To standardowa technika, którą stosują komercyjne trackery,
i mieści się w budżecie transferu z 1.6.

## 2.6 Kolejka offline

Punkty trafiają do kolejki FIFO w LittleFS (rekord stały 32 B, plik pierścieniowy).
Pojemność projektowa: 20 000 punktów, czyli około 7 dni ciągłej jazdy z interwałem 30 s.
Po odzyskaniu łącza kolejka wysyła się partiami po 50 punktów na temat `.../batch`,
z oryginalnym `ts`, a HA wstawia je do historii jako punkty przeszłe.

Dopiero potwierdzenie MQTT QoS 1 (PUBACK) kasuje rekord z kolejki. Utrata zasilania
w środku dosyłki daje duplikaty, nie ubytki, a duplikaty odfiltrowuje integracja po `seq`.

## 2.7 Czas

Znacznik `ts` pochodzi z GNSS (UTC z ramki NMEA), a nie z zegara ESP32, bo ESP32 nie ma
RTC z podtrzymaniem po odcięciu zasilania. Gdy fixa nie ma, `ts` pochodzi z SNTP przez
LTE, a pakiet dostaje flagę `ts_src: "ntp"`. Gdy nie ma ani jednego, punkt idzie
z `ts: 0` i integracja stempluje go czasem odbioru, oznaczając atrybutem.

## 2.8 Podział odpowiedzialności HA kontra firmware

Firmware nie liczy przejazdów, nie liczy dystansu i nie zna geofence. Wysyła surowe
punkty i stany. Wszystkie wyższe funkcje liczy integracja w HA, bo tam łatwo je zmienić
bez wyjmowania urządzenia z auta. Jedyny wyjątek to alarm MOVED_WHILE_PARKED, który
musi zadziałać nawet gdy HA jest wyłączony, więc decyzję podejmuje firmware.
