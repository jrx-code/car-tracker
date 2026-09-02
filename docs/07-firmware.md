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
| `lilygo_a7670` | D, gotowa płytka | jak `a7670e` plus `BOARD_LILYGO_TA7670` |

Stan na dziś: **wszystkie pięć środowisk kompilują się** (PlatformIO 6.1.19,
platforma espressif32 55.03.39, Arduino core 3.3.9). Budowa `wifi_dev` zajmuje
59,9 procent flasha i 13,9 procent RAM przy starcie, więc jest zapas na fazę 2.

Uwaga na `MODEM_HAS_GNSS=0` w środowiskach A7670: to nie jest przeoczenie, tylko
konsekwencja tego, że o obecności GNSS w tej rodzinie decyduje sufiks modułu
(FASE ma, LASE nie ma). Po zakupie i sprawdzeniu oznaczenia flagę się podnosi.

## 7.2 Zależności i jedna niespodzianka

| Biblioteka | Po co |
|---|---|
| `knolleary/PubSubClient` | MQTT |
| `bblanchon/ArduinoJson` | pakiety, patrz 05 |
| `mikalhart/TinyGPSPlus` | NMEA z NEO-6M |
| `mobizt/ESP_SSLClient` | TLS |
| `vshymanskyy/TinyGSM` | modem, tylko w środowiskach LTE |

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
