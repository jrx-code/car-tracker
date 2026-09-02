# 12. Bring-up: pierwsze uruchomienie na biurku

Stan na 2026-09-02. To jest dziennik faktów, nie plan: wpisujemy tu tylko to,
co zostało zrobione i zmierzone.

## 12.1 Płytka, którą mamy

Odczytane z `esptool` przez `/dev/ttyUSB0`, nie z opisu na opakowaniu:

| | |
|---|---|
| Układ | ESP32-D0WD-V3, rewizja v3.1 |
| Rdzenie | dwurdzeniowy, 240 MHz |
| Radio | WiFi + Bluetooth |
| Flash | 4 MB, 3,3 V |
| MAC | `aa:bb:cc:dd:ee:ff` |
| Konwerter USB | Silicon Labs CP2102 |
| Port | `/dev/ttyUSB0` |
| Kalibracja Vref | obecna w eFuse (przyda się przy pomiarze napięcia, docs/04 punkt 4.4) |

4 MB flasha wystarcza: `wifi_dev` zajmuje 785 kB, a kolejka offline
(20 000 rekordów po 30 B) to około 600 kB w LittleFS.

## 12.2 Bluetooth jako transport: sprawdzone i odrzucone

Pomysł "zamiast hotspotu WiFi weźmy Bluetooth z telefonu" nie przechodzi,
i to z dwóch niezależnych powodów:

1. **ESP-IDF nie wspiera profilu PAN ani protokołu BNEP.** Wspierane profile
   Bluetooth Classic to A2DP, AVRCP, SPP, HFP, HID i PBAP-Client. Bez BNEP nie
   ma mostu warstwy łącza, czyli nie ma dostępu do sieci IP. Da się to zrobić
   podmieniając cały stos Bluetooth na BTStack, który ma demo PAN, ale to
   znaczy inny stos w projekcie, którego jedynym zadaniem jest zastąpić coś,
   co WiFi robi w trzech linijkach.
2. **W tym konkretnym Arduino core nie ma nawet bibliotek Bluetooth.** Katalog
   `framework-arduinoespressif32/libraries` w wersji 3.3.9 nie zawiera ani
   `BluetoothSerial`, ani `BLE` (sprawdzone `ls`, katalogów nie ma). Każde
   użycie BT zaczyna się tu od dokładania zależności.

**Decyzja: transportem PoC jest WiFi**, hotspot z telefonu w aucie i domowe
WiFi w garażu. Bluetooth wraca do rozważenia dopiero, gdyby PoC pokazał, że
hotspot jest niewygodny na tyle, żeby uzasadnić pracę z BTStack.

## 12.3 Narzędzie: `firmware/probe`

Osobny mały projekt PlatformIO, celowo niezależny od głównego firmware: nie
potrzebuje `config.h`, WiFi ani brokera. Oddziela pytanie "czy odbiornik jest
dobrze podłączony" od "czy reszta jest dobrze skonfigurowana".

```bash
cd firmware/probe
pio run -t upload
pio device monitor          # 115200
```

Kolejno odpowiada na pięć pytań: czy cokolwiek przychodzi na pin RX, przy jakiej
prędkości, czy ramki są poprawnym NMEA, czy odbiornik widzi satelity, czy ma fix
i jak dobry. To rozróżnienie ma znaczenie praktyczne: **zamienione TX i RX dają
zero bajtów, a zła prędkość daje bajty bez poprawnych ramek.** W komunikacie
"brak danych GPS" oba wyglądają identycznie i traci się na tym godziny.

W konsoli działa `r` (włącz i wyłącz echo surowego NMEA) oraz `s` (ponowny skan).

Stan zweryfikowany bez podłączonego modułu: pięć prędkości, zero bajtów na każdej,
komunikat z listą przyczyn w kolejności prawdopodobieństwa.

## 12.4 Gotcha znaleziony przy budowie probe

Pierwsza wersja skanera robiła `gpsSerial.begin()` i `gpsSerial.end()` przy każdej
próbowanej prędkości. Efekt: **1,8 MB zapętlonego, powtarzającego się tekstu na
konsoli USB w 20 sekund** zamiast wyniku skanu. `HardwareSerial::end()` w Arduino
core 3.3.9 woła `uartEnd()`, który odpina wszystkie piny i usuwa sterownik UART,
a cykliczne odtwarzanie UART1 w pętli rozwala strumień konsoli.

Poprawka: jedno `begin()` w `setup()`, a w skanie wyłącznie `updateBaudRate()`.
Po poprawce ten sam przebieg to 1180 bajtów czytelnego wyniku.

Dwa wnioski na przyszłość, bo to samo dotyczy głównego firmware:
- Nie wołać `end()` na UART, żeby zmienić prędkość. Od tego jest `updateBaudRate()`.
- Objaw "konsola pluje bez sensu" niekoniecznie znaczy zły baud rate konsoli.
  Tutaj konsola była w porządku, a psuł ją kod zarządzania innym UART-em.

Dodatkowo pin RX ma `INPUT_PULLUP`. Linia UART w spoczynku jest wysoka, więc bez
podciągnięcia niepodłączone wejście łapie szum i skaner melduje bajty, których
nie ma. Z podciągnięciem "nic nie podłączone" znaczy dokładnie zero bajtów.

## 12.5 Podłączenie NEO-6M

Cztery przewody. Piny zgodne z `firmware/include/pins.h`, więc to samo
połączenie obsłuży probe i główny firmware.

| Moduł GPS | ESP32 | Uwaga |
|---|---|---|
| **VCC** | **3V3** | patrz niżej, zaczynamy od 3,3 V |
| **GND** | **GND** | wspólna masa jest obowiązkowa, bez niej UART nie działa mimo poprawnych danych |
| **TX** | **GPIO26** | wyjście modułu do wejścia ESP32 |
| **RX** | **GPIO27** | wejście modułu, w praktyce nieużywane, ale podłącz |

**Zasilanie: zacznij od 3V3.** Sam układ u-blox NEO-6M pracuje przy 3,3 V.
Płytki typu GY-NEO6MV2 mają własny stabilizator i opis "3V-5V", więc działają
też z 5 V, ale 3,3 V jest wariantem, który nie zaszkodzi w żadnym z tych
przypadków. Jeżeli przy 3,3 V skaner pokazuje zero bajtów, a napięcie na module
zmierzone miernikiem jest poprawne, dopiero wtedy spróbuj 5 V (pin `VIN`/`5V`
na DevKit, zasilany z USB).

Czego nie robić: nie zasilaj modułu z pinu GPIO. GPIO w ESP32 daje kilkanaście
miliamperów, a NEO-6M w trakcie akwizycji bierze kilkadziesiąt.

**Antena:** ceramiczna antena płytki albo antena zewnętrzna musi widzieć niebo.
Na biurku w środku mieszkania fixa może nie być wcale. Do pierwszego testu
połóż moduł na parapecie otwartego okna albo wystaw na balkon.
Zimny start NEO-6M pod otwartym niebem to około 27 s, w gorszych warunkach
kilka minut. Dioda na module zaczyna mrugać dopiero po złapaniu fixa, więc
brak mrugania przez pierwszą minutę nie jest jeszcze objawem awarii.

## 12.6 Co dalej

Po podłączeniu: `pio device monitor` w `firmware/probe` i zapisanie wyników
(liczba satelitów, HDOP, czas do pierwszego fixa) do `hardware/pomiary.md`.
Te liczby są wejściem do decyzji, czy NEO-6M zostaje, czy w wersji docelowej
potrzebny jest odbiornik wielosystemowy (docs/03 punkt 3.1).
