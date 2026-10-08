# 07. Firmware

## 7.1 Budowa i środowiska

```bash
cd firmware
cp src/config.example.h src/config.h     # wypełnij, plik jest w .gitignore
pio run -e wifi_dev -t upload            # bench, bez modemu
pio run -e sim7670g -t upload            # wariant A
```

| Środowisko | Wariant z rozdziału 03 | Flagi |
|---|---|---|
| `wifi_dev` | brak modemu, bench | `TRANSPORT_WIFI` |
| `sim7670g` | A, rekomendowany | `TINY_GSM_MODEM_SIM7600`, `MODEM_HAS_GNSS=1` |
| `a7670e` | A7670E/G | `TINY_GSM_MODEM_A7672X`, `MODEM_HAS_GNSS=0` |
| `sim7080g` | B, LTE-M/NB-IoT | `TINY_GSM_MODEM_SIM7080`, `MODEM_SUPPORTS_PSM` |
| `sim7600e` | C, Cat-4 | `TINY_GSM_MODEM_SIM7600` |
| `lilygo_a7670` | D, gotowa płytka | jak `a7670e` plus `BOARD_LILYGO_TA7670`, `MODEM_HAS_GNSS=1` |
| `lilygo_wifi` | D, dane przez WiFi | `TRANSPORT_WIFI`, `BOARD_LILYGO_TA7670`, `MODEM_HAS_GNSS=1` |

`lilygo_wifi` to płytka LilyGO z danymi po WiFi: modem jest zasilany wyłącznie jako
odbiornik GNSS (`AT+CGNSSINFO`), bez logowania do sieci. Na biurko i na kartę SIM bez
transmisji danych.

Stan na dziś: **wszystkie siedem środowisk kompiluje się** (PlatformIO 6.1.19,
platforma espressif32 55.03.39, Arduino core 3.3.9). Budowa `wifi_dev` zajmuje
59,9 procent flasha i 13,9 procent RAM przy starcie, więc jest zapas na fazę 2.

Uwaga na `MODEM_HAS_GNSS=0` w `a7670e`: o obecności GNSS w tej rodzinie decyduje
sufiks modułu (FASE ma, LASE nie ma). Płytka LilyGO kupiona do projektu ma
**A7670E-FASE** (fw `A7670M7_B09V01_250619`, sprawdzone 2026-09-28), więc w obu
środowiskach `lilygo_*` flaga jest podniesiona.

### 7.1.1 LilyGO T-A7670E: co wyszło na biurku (2026-09-28)

- **GNSS siedzi w modemie**, nie na osobnym UART ESP32. TinyGSM 0.12 nie ma dla
  A7672X żadnego API GNSS, więc odczyt idzie gołym AT i parserem
  `src/modem/cgnssinfo.h`. Parser sam rozpoznaje liczbę pól i format szerokości
  (ddmm albo stopnie). `[DO ZWERYFIKOWANIA na prawdziwym fixie]`: układ pól
  i jednostka prędkości; na biurku widziana była tylko linia bez fixu.
- **Pin mapa wzięta z definicji producenta** (`utilities.h`, blok `LILYGO_T_A7670`).
  Poprzednia wersja miała LED na GPIO 12, który jest zasilaniem modemu, oraz
  akcelerometr na pinach gniazda micro SD.
- **GPIO 35 to dzielnik ogniwa Li-ion płytki, nie napięcie z OBD.** Przeliczony
  kalibracją OBD dawał 12,85 V na zasilaniu z USB i usypiał płytkę w PARKED.
  Do czasu własnego dzielnika z OBD `PIN_VBAT_ADC` jest -1 (ścieżka „bez dzielnika").
- **TLS czeka na zegar.** BearSSL sprawdza daty certyfikatu względem `time()`;
  przed NTP to 1970. WiFi czeka na NTP, LTE podaje czas z sieci przez `setX509Time`.
- **Zegar modemu bywa w roku 2070** (2026-10-08). A7670 bez czasu z sieci podaje
  `70/01/01`, co dwucyfrowy rok zamienia na 2070, a `toUnixUtc` przyjmował lata do
  2099. Certyfikat brokera wyglądał na wygasły i uzgadnianie kończyło się bez alertu.
  Aktualizacja czasu z sieci (`AT+CTZU`) była wyłączona fabrycznie. Teraz: `CTZU=1`
  przy włączeniu modemu, zapasowo NTP w modemie (`AT+CNTP`), lata poza 2020-2060
  są odrzucane.
- **TLS zawsze jawnie** (2026-10-08). `ESP_SSLClient::connect()` robi TLS tylko dla
  portów z wbudowanej listy (443, 8883, ...) i na każdy inny port wysyła tekst
  jawny, bez błędu. Na brokerze na porcie spoza listy hasło MQTT poszło otwartym
  tekstem. Klient jest teraz tworzony z wyłączonym SSL, a `openBrokerSocket()`
  w `modem/broker_socket.h` sam robi `connectSSL()`, gdy `mqtt_tls` jest włączone,
  i zamyka gniazdo przy nieudanym uzgadnianiu, zanim PubSubClient wyśle bajt.
- **TinyGSM 0.12 nie otwiera zwykłego gniazda na A7670.** Wysyła `CTCPKA` przed
  `NETOPEN` (pierwsze daje ERROR przed otwarciem sieci, drugie po), a po `CIPSEND`
  czeka na `+CCHSEND`, które zgłasza tylko gniazdo SSL modemu. Łata nakładana przed
  buildem: `scripts/patch_tinygsm_a7672x.py`; build pada, jeśli łatany kod się
  zmieni.
- **Pin RESET modemu** (GPIO 5, aktywny stanem wysokim, pin konfiguracyjny ESP32)
  nie był niczym sterowany. Pierwsza rejestracja po starcie płytki padała 4 razy
  na 4 (`CREG 0`, `CSQ 99` przez całe oczekiwanie), surowe AT rejestrowało się
  w 15-29 s. RESET jest teraz trzymany nisko przy włączaniu; skutek
  `[DO ZMIERZENIA]`, jedna próba w terenie: online 31 s po starcie. Przy
  nieudanej rejestracji firmware wypisuje `CFUN`, `CPIN`, `CEREG`, `COPS`, `CPSI`
  i `CGNSSPWR`.
- **Bez dzielnika i bez akcelerometru tryb decyduje GNSS.** Inaczej płytka z PoC
  na powerbanku zostawała w PARKED i nigdy nie zapisywała śladu. Odbiornik pracuje
  stale, dwa odczyty >= 10 km/h otwierają przejazd, < 3 km/h przez 3 min albo brak
  fixu przez 10 min go zamykają. Progi `[DO ZMIERZENIA]`.
- **Certyfikat do portalu wysyłać jako `text/plain`.** Wysłany jako formularz
  (domyślny `curl --data-binary`) zamienia `+` na spacje, zapisuje się bez błędu
  i daje zero kotwic. Portal teraz odrzuca taki PEM.
- **Portal żyje w trakcie łączenia LTE** (2026-10-01, zgłoszenie #10). TinyGSM
  czeka na odpowiedź modemu do 60 s na komendę (CGACT, CGATT, NETCLOSE), a karta
  bez danych siedzi w tych oczekiwaniach minutami. `TINY_GSM_YIELD()` jest
  przekierowane na hook, który w fazach włączania, rejestracji i PDP woła
  `portal::loop()`; uzgadnianie TLS jest z tego wyłączone. Kod wywołany z hooka
  nie wysyła AT, odpowiada z ostatniego stanu. Bez zestawionego PDP transport nie
  dotyka MQTT ani modemu, a czas z sieci jest pamiętany i odpytywany najwyżej raz
  na minutę. Pomiar na karcie bez danych, `/api/status` co sekundę przez 5 min:
  przed zmianą 0 z 40 odpowiedzi, po zmianie 235 z 236, mediana 0,13 s.
  Pojedyncze odpowiedzi 4-5 s zostały, przy szybkim TCP connect i bez żadnej
  przerwy powyżej 400 ms w firmware; jedna taka była też w wariancie samego WiFi.
  Przyczyna niewyjaśniona.

## 7.2 Zależności i jedna niespodzianka

| Biblioteka | Po co |
|---|---|
| `knolleary/PubSubClient` | MQTT |
| `bblanchon/ArduinoJson` | pakiety, patrz 05 |
| `mikalhart/TinyGPSPlus` | NMEA z NEO-6M |
| `mobizt/ESP_SSLClient` | TLS |
| `vshymanskyy/TinyGSM` | modem, tylko w środowiskach LTE; dla A7672X łatany przed buildem (7.1) |

TLS **nie** idzie przez `WiFiClientSecure`. Arduino core 3.3.9 w tej platformie
nie dostarcza biblioteki `NetworkClientSecure` ani nagłówka `WiFiClientSecure.h`
(sprawdzone w `~/.platformio/packages/framework-arduinoespressif32/libraries`,
katalogu nie ma). Zamiast szukać obejścia dla samego WiFi, TLS jest zrobiony
wrapperem `ESP_SSLClient` na dowolnym `Client`. Efekt uboczny jest korzystny:
ta sama ścieżka weryfikacji certyfikatu działa na WiFi i na modemie, a CA nie
trzeba wgrywać do modemu komendami AT specyficznymi dla producenta.

Bufory BearSSL ustawione na 4096 na odbiór i 1024 na nadawanie. Mniejszy bufor
odbioru działa tylko wtedy, gdy broker honoruje `max_fragment_length`,
a tego nie zakładamy bez sprawdzenia.

## 7.3 Struktura

```
src/
  main.cpp              maszyna stanow, jedyne miejsce decyzji
  state.h               PosRecord (30 B, packed), Telemetry, Config
  config.example.h      wzorzec, prawdziwy config.h w gitignore
  modem/
    transport.h         interfejs "wyslij bajty na temat"
    transport_wifi.cpp  bench i OTA
    transport_lte.cpp   TinyGSM, wszystkie warianty SIMCom
  gnss/gnss.*           NMEA, wybor zrodla, filtr HDOP
  power/power.*         ADC, load switche, deep sleep
  power/motion.*        LIS3DH, wlasny sterownik na rejestrach
  telemetry/store.*     kolejka pierscieniowa w LittleFS
  telemetry/packet.*    JSON zgodny z docs/05
  util/timeutil.h       konwersja UTC bez timegm()
include/pins.h          mapa pinow per wariant plytki
```

## 7.4 Decyzje, które łatwo odwrócić przez pomyłkę

**Brak `timegm()` w newlib ESP32.** Konwersja daty na czas uniksowy idzie przez
`timeutil::toUnixUtc` z algorytmem `days_from_civil`. Kuszące jest użycie
`mktime()`, ale to czas lokalny i przesunęłoby całą historię pozycji o strefę.

**`PosRecord` ma 30 bajtów, nie 28.** `static_assert` w `state.h` pilnuje rozmiaru,
bo rekord jest zapisywany do pliku bajt w bajt. Zmiana układu pola bez podbicia
`kStoreVersion` w `store.cpp` oznacza, że stara kolejka zostanie odczytana jako
śmieci i wysłana do HA jako pozycje. Dlatego niezgodna wersja czyści kolejkę.

**Rekord znika z kolejki dopiero po PUBACK.** Awaria w trakcie dosyłki daje
duplikaty, nie ubytki. Deduplikacja po `seq` jest po stronie HA. Odwrotna
kolejność (najpierw skasuj, potem wyślij) gubi dane po każdym zaniku zasilania.

**`seq` zapisywany do NVS co 16 sztuk, nie co jedną.** NVS ma skończoną liczbę
kasowań. Dziura w numeracji po zaniku zasilania jest nieszkodliwa, zużyty flash
po pół roku jazdy już nie.

**Modem jest odcinany, nie usypiany.** `power::deepSleep()` najpierw gasi obie
szyny. To jedyny sposób na budżet z rozdziału 04. Uśpienie modemu zostawia
setki mikroamperów na stałe.

**OTA po LTE jest świadomie niezaimplementowane.** Obraz firmware przez łącze
taryfowe, w jadącym aucie, to prosta droga do cegły na parkingu podziemnym.
Komenda `ota` odpowiada odmową, aktualizacja idzie po WiFi w garażu.

**Progi napięcia mają twarde ograniczenia w `applyConfig`.** Konfiguracja
przychodzi z sieci i nie może ustawić hibernacji poniżej 11,0 V ani odwrócić
histerezy. Zła konfiguracja nie może doprowadzić do rozładowania akumulatora auta.

## 7.5 Kalibracja ADC

Krok W4 planu wdrożenia. Na stole, zasilacz laboratoryjny, dwa punkty
(na przykład 11,5 V i 14,5 V), odczyt surowej wartości ADC i wywołanie
`power::setCalibration()`. Bez tego napięcie w HA jest orientacyjne,
bo domyślne wzmocnienie wynika z teoretycznego dzielnika i nominalnej skali ADC,
a ADC w ESP32 jest nieliniowy.

## 7.6 Czego firmware nie robi

Nie liczy przejazdów, dystansu ani geofence. To jest w HA i tam się to poprawia
bez wyjmowania urządzenia z auta. Wyjątek: alarm ruchu na postoju, bo musi
zadziałać także wtedy, gdy HA nie działa.

## 7.7 Aktualizacja przez sieć (OTA)

Obraz idzie tą samą sesją MQTT po TLS, którą tracker już ma, po LTE albo po WiFi.
Nie ma osobnego portu ani hosta do pobierania: broker jest jedyną rzeczą
osiągalną z sieci operatora, więc hub wysyła obraz kawałkami przez niego.

1. `scripts/ota_release.sh <env> <pojazd>` buduje obraz, podpisuje skrót SHA-256
   kluczem ECDSA P-256 (klucz prywatny poza repo) i wysyła go do huba
   (`POST /api/vehicles/<id>/ota`, nagłówek `X-Firmware-Signature`). Skrypt odmawia
   przy niezacommitowanych zmianach, bo `fw` w pakiecie `info` to hash commita,
   i przy kluczu niezgodnym z `src/ota/ota_pubkey.h`.
2. Hub zapisuje obraz i wysyła komendę `ota` z rozmiarem, skrótem i podpisem.
3. Tracker przyjmuje ją tylko na postoju (nigdy w trybie DRIVING ani MOVED),
   przy `ota_enabled` i gdy obraz mieści się w slocie. **Podpis sprawdza przed
   pobraniem pierwszego bajtu.** Sama suma by nie wystarczyła: każde konto
   `cartracker-*` może publikować w całym `cartracker/#`.
4. Tracker prosi o kolejne kawałki (`ota/req` z offsetem), hub odpowiada na
   `ota/data` (4 bajty offsetu little-endian plus dane). Kawałek z innym offsetem
   jest ignorowany, brakujący zamawiany ponownie (4 próby po 15 s).
5. Zapis idzie do nieaktywnego slotu OTA. Przerwane pobieranie zostawia działający
   firmware nietknięty. Po komplecie tracker porównuje skrót z podpisanym,
   `Update.end()` sprawdza nagłówek obrazu i przełącza slot startowy.
6. **Próbny start.** Bootloader w tym rdzeniu nie ma rollbacku
   (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` wyłączone), więc pilnuje tego firmware:
   nowy obraz musi połączyć się z brokerem w 10 minut i nie restartować się więcej
   niż 3 razy, inaczej `Update.rollBack()` wraca do poprzedniego slotu. Udany
   start wysyła zdarzenie `ota_confirmed`.

Postęp i wynik przychodzą jako `ack` na komendę `ota`; nieudane `locate` podaje
ostatnią surową linię `+CGNSSINFO`. `TINY_GSM_RX_BUFFER` podniesiony do 1024:
z domyślnym 64 każdy rekord TLS to kilkadziesiąt wymian AT z modemem.

`[DO ZMIERZENIA]` czas przesłania obrazu ~0,9 MB po LTE i zużycie danych; całość
nie była jeszcze uruchomiona na płytce. Pierwsze wgranie z OTA idzie przez USB.
