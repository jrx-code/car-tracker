# 03. Hardware: warianty i BOM

Wszystkie cztery warianty modemu są rozpatrzone, bo decyzja zakupowa jeszcze nie zapadła,
a firmware ma warstwę abstrakcji (`ITransport`) i osobne środowisko build dla każdego.
Zmiana wariantu po zakupie to zmiana jednego `-e` w `pio run`, nie przepisywanie projektu.

## 3.1 Co jest na stanie

| Element | Model | Uwagi |
|---|---|---|
| MCU | ESP32 (WiFi + BT) | `[DO SPRAWDZENIA: dokładny wariant płytki, DevKitC czy WROOM na własnej płytce. Ma to wpływ na budżet prądowy, patrz 04]` |
| GNSS | NEO-6M-0-001 | u-blox 6, **GPS L1 C/A tylko**, bez GLONASS/Galileo/BeiDou. Dokładność około 2,5 m, zimny start około 27 s |
| Modem LTE | brak | przedmiot decyzji poniżej |

**Ograniczenie NEO-6M, które trzeba znać przed budową:** to konstrukcja jednosystemowa.
W mieście, w kanionie ulicznym i pod drzewami ma zauważalnie gorszą dostępność fixa niż
odbiorniki wielosystemowe, bo widzi tylko konstelację GPS. Nowsze u-blox M8N i M10 śledzą
GPS, GLONASS, Galileo i BeiDou jednocześnie, mają dokładność około 1,5 m CEP,
a M10 zimny start rzędu 10-15 s.

Wniosek praktyczny: NEO-6M jest w porządku na fazę bench i na v1 w aucie, ale nie jest
docelowy. W wariantach, gdzie modem ma własny GNSS, NEO-6M zostaje jako źródło zapasowe
i element porównawczy, a nie jako jedyny odbiornik.

## 3.2 Wariant A: SIM7670G (Cat-1 bis, GNSS) - REKOMENDACJA

Chipset Qualcomm QCX216, LTE Cat 1 bis, do 10 Mbps w dół i 5 Mbps w górę, wbudowany GNSS.
Pobór podawany przez producenta: 4,2 mA w trybie idle i 800 µA w trybie sleep przy DRX 0,64 s.
Zasilanie modułu 3,4-4,2 V, płytki breakout typowo 5 V z zapewnionym prądem 0,8 A.
Zestaw komend AT jest zgodny z rodzinami A7670, SIM7000/SIM7070 i SIM800.

**Za:**
- Cat-1 bis działa na zwykłym LTE, tym samym którego używa telefon. Nie zależy od tego,
  czy operator uruchomił LTE-M albo NB-IoT, i nie ma problemu z roamingiem w UE (Z5).
- Jedna antena LTE (to znaczy "bis"), czyli mniej miejsca i jeden przepust w obudowie.
- Własny GNSS, więc NEO-6M staje się redundancją, a nie pojedynczym punktem awarii.
- Zgodność AT z resztą rodziny SIMCom, więc kod transportu jest w dużej mierze wspólny.

**Przeciw:**
- 800 µA modemu w sleep to i tak dziesięć razy więcej niż wariant B, więc w PARKED
  modem musi być odcinany load switchem, a nie tylko usypiany.
- Prąd szczytowy przy TX wymaga kondensatora buforowego przy module, inaczej reset
  ESP32 przy rejestracji w sieci.

## 3.3 Wariant B: SIM7080G (LTE-M / NB-IoT, GNSS)

Chipset Qualcomm MDM9205, LTE Cat M1 i NB-IoT NB1/NB2, PSM i eDRX, GNSS opcjonalny.
Rodzina LPWA nastawiona na wieloletnią pracę z baterii.

**Za:**
- Najniższy pobór z całej stawki. W PSM to poziom mikroamperów, co samo w sobie
  spełniłoby założenie Z1 nawet bez odcinania zasilania.
- Małe pakiety telemetryczne to dokładnie profil ruchu, do którego LTE-M jest zrobione.

**Przeciw, i to jest powód, dla którego nie jest to rekomendacja:**
- **SIM7080G nie ma fallbacku 2G/EGPRS.** Ma go dopiero SIM7070G z tej samej rodziny.
  Tracker bez LTE-M w danym miejscu i bez 2G po prostu milczy.
- Pokrycie LTE-M i NB-IoT jest nierówne, a roaming tych technologii w UE bywa
  niedostępny w taryfach konsumenckich. Auto na wyjeździe to scenariusz z Z5.
  `[DO WERYFIKACJI u operatora przed zakupem: czy taryfa obejmuje LTE-M, czy działa w roamingu]`
- NB-IoT ma duże opóźnienia i ograniczenia po stronie sieci, co gryzie się z alarmem
  MOVED_WHILE_PARKED, gdzie zależy nam na sekundach.

**Kiedy jednak wybrać B:** gdyby projekt zmienił się w tracker bateryjny bez OBD,
przyklejany gdzieś w aucie, gdzie zasilanie jest realnym ograniczeniem, a alarm
w czasie rzeczywistym przestaje być wymogiem. Jeśli B, to raczej **SIM7070G**
(ta sama klasa, ale z 2G jako siatką bezpieczeństwa) niż sam SIM7080G.

## 3.4 Wariant C: SIM7600E-H (Cat-4, GNSS)

**Za:** najszersze pasma, najlepsza dokumentacja i największa baza przykładów,
GNSS wielosystemowy, przepustowość z zapasem na OTA przez LTE.

**Przeciw:** największy pobór i największy prąd szczytowy w całej stawce, największa
płytka, wyższa cena. Cat-4 to przepustowość, której ten projekt nie użyje: pakiet
telemetryczny ma 120 bajtów. Płacimy prądem i miejscem za parametr bez zastosowania.

**Kiedy wybrać C:** gdy projekt urósłby o kamerę albo o wysyłanie zdjęć z alarmu.

## 3.5 Wariant D: gotowa płytka LilyGO T-A7670 / T-SIM7080G

ESP32 razem z modemem, PMU, slotem SIM, złączami antenowymi i ładowarką ogniwa na jednej
płytce. Rodzina A7670 to LTE Cat 1, LTE-FDD/TDD z fallbackiem GSM/GPRS/EDGE, do 10 Mbps
w dół i 5 Mbps w górę, obudowa LGA 24 x 24 x 2,3 mm.

**Uwaga zakupowa, łatwa do przeoczenia:** w rodzinie A7670 o obecności GNSS decyduje
sufiks wersji, nie sama nazwa. Warianty **FASE** mają GNSS i Bluetooth, warianty
**LASE** nie mają GNSS. Kupno "A7670E" bez sprawdzenia sufiksu może dać moduł bez GPS.
Płytki LilyGO często dokładają osobny odbiornik GNSS właśnie z tego powodu.
`[DO WERYFIKACJI przy zakupie: pełne oznaczenie modułu na płytce i czy GNSS jest w module, czy jako osobny układ]`

**Za:**
- Najmniej lutowania i najmniej okazji do błędu w zasilaniu, a zasilanie jest tu
  najtrudniejszą częścią (rozdział 04).
- PMU na płytce ma pomiar napięcia i sterowanie zasilaniem modemu, czyli dokładnie to,
  czego wymaga budżet prądowy.
- Twój gołe ESP32 zostaje wolny na inny projekt.

**Przeciw:**
- Mniejsza kontrola nad prądem spoczynkowym: to, co producent wlutował, zostaje.
  `[DO ZMIERZENIA po zakupie: rzeczywisty pobór płytki w deep sleep, deklaracje producentów bywają optymistyczne]`
- Ładowarka LiPo w aucie stojącym latem na słońcu to ryzyko, którego nie chcemy.
  Jeżeli wariant D, to ogniwo LiFePO4 albo brak ogniwa i zaślepienie ładowarki.

## 3.6 Zestawienie decyzyjne

| Kryterium | A: SIM7670G | B: SIM7080G | C: SIM7600E-H | D: LilyGO |
|---|---|---|---|---|
| Sieć | LTE Cat-1 bis | LTE-M / NB-IoT | LTE Cat-4 | LTE Cat-1 |
| Fallback 2G | zależny od wersji | **brak** | jest | jest (EDGE) |
| Roaming UE w praktyce | dobry | ryzykowny | dobry | dobry |
| GNSS w module | tak | opcjonalny | tak | zależny od sufiksu |
| Pobór idle / sleep | 4,2 mA / 800 µA | najniższy w stawce | najwyższy | jak modem plus PMU |
| Prąd szczytowy | średni | niski | wysoki | średni |
| Pracochłonność montażu | średnia | średnia | średnia | **najniższa** |
| Ryzyko projektu | niskie | **wysokie (pokrycie)** | niskie | niskie technicznie, średnie zakupowo |

**Rekomendacja:** wariant A na własnej płytce, albo wariant D, jeśli priorytetem jest
szybkie dojście do działającego urządzenia w aucie. Oba są w firmware obsłużone.
Wariant B tylko z SIM7070G zamiast SIM7080G i tylko po potwierdzeniu pokrycia u operatora.

## 3.7 BOM wspólny (niezależny od wariantu modemu)

| Poz. | Element | Ilość na 1 auto | Po co |
|---|---|---|---|
| 1 | Wtyk OBD-II J1962 męski z obudową | 1 | Z3, wypięcie bez narzędzi |
| 2 | Bezpiecznik polimerowy 500 mA albo topikowy w oprawce | 1 | ochrona instalacji auta, nie trackera |
| 3 | Dioda TVS SMBJ24A | 1 | skoki napięcia przy rozruchu i load dump |
| 4 | Dioda Schottky albo P-MOSFET ideal diode | 1 | ochrona przed odwrotną polaryzacją |
| 5 | Przetwornica step-down o niskim prądzie spoczynkowym | 1 | patrz 04, to jest kluczowy element budżetu |
| 6 | Load switch P-MOSFET na zasilaniu modemu i GNSS | 2 | odcięcie, nie usypianie |
| 7 | Akcelerometr LIS3DH albo MPU6050 z przerwaniem | 1 | wake on motion, detekcja jazdy i holowania |
| 8 | Dzielnik napięcia 12 V na ADC z filtrem RC | 1 | pomiar napięcia akumulatora |
| 9 | Kondensator elektrolityczny 470-1000 µF przy modemie | 1 | prąd szczytowy TX |
| 10 | Antena GNSS aktywna z magnesem, kabel 1,5 m | 1 | pod podszybie, nie w kokpicie |
| 11 | Antena LTE, klejona albo z magnesem | 1 | tak samo |
| 12 | Obudowa drukowana | 1 | `hardware/obudowa/` |

Anteny są w BOM celowo osobno: umieszczenie anteny GNSS decyduje o jakości fixa bardziej
niż wybór między NEO-6M a M10. Wtyk OBD siedzi pod deską, gdzie jest metal i zero widoku
nieba, więc antena musi wyjść na podszybie.

## 3.9 Decyzja zakupowa: co konkretnie kupić na GPS i LTE

Punkty 3.2-3.6 porównywały klasy modułów. To jest wybór konkretnej płytki,
oparty na oficjalnym przeglądzie serii SIMCom, a nie na opisach sprzedawców.

### Kupujemy: LilyGO T-A7670**E** R2, wersja **z GPS (L76K)**

To jest wariant D z 3.5, doprecyzowany. Jedna płytka daje ESP32-WROVER-E, modem,
GNSS, slot SIM, PMU i złącza antenowe.

### Dlaczego E, a nie G ani SA

Pasma z przeglądu SIMCom „A7670 Series Overview v2020.02", tabela porównawcza:

| Wariant | LTE-FDD | GSM | Werdykt |
|---|---|---|---|
| A7670C | B1/B3/B5/B8 | 900/1800 | **nie**, brak B7 i B20 |
| **A7670E** | **B1/B3/B5/B7/B8/B20** | 900/1800 | **to bierzemy** |
| A7670SA | B1/B2/B3/B4/B5/B7/B8/B28/B66 | 850/900/1800/1900 | nie, Ameryka Płd. i Australia |

Rozstrzyga **B20**, czyli LTE 800 MHz. To jest pasmo, na którym stoi zasięg poza
miastem, a auto ma jeździć w trasę (Z5). A7670C i A7670SA go nie mają.

**A7670G w tym przeglądzie nie występuje.** Nie znalazłem dla niego pierwotnego
źródła z listą pasm, więc mimo że sprzedawcy opisują go jako „global", kupowanie
go byłoby zgadywaniem. `[DO WERYFIKACJI, gdyby ktoś chciał wariant G: lista pasm
z dokumentu SIMCom, nie z opisu oferty.]`

Wszystkie trzy warianty mają GSM/GPRS/EDGE, czyli **fallback 2G jest**. Dla
trackera to nie jest luksus: LTE Cat-1 bez 2G w martwej strefie po prostu milczy.

### Pułapka: o GNSS decyduje sufiks, nie nazwa

Przegląd SIMCom nie ma wiersza GNSS ani GPS, jest tylko `LBS`, czyli lokalizacja
z masztów, a to nie jest pozycja. Ale ten dokument **nie rozbija sufiksów
modułu**, a to one rozstrzygają:

| Oznaczenie | GNSS |
|---|---|
| A7670E-**LASE** | **nie** |
| A7670E-**FASE** | **tak**, GPS, GLONASS, BeiDou w module |

Czyli „A7670E" w tytule oferty nie mówi nic o GPS. Sprzedawcy, którzy wiedzą, co
sprzedają, podają pełne oznaczenie; ci, którzy nie podają, mogą mieć jedno albo
drugie. `platformio.ini` ma to zapisane od początku przy wariancie `a7670e`:
`MODEM_HAS_GNSS` zostaje 0, dopóki pełne oznaczenie nie zostanie potwierdzone.

Drugi wariant tego samego problemu: na płytkach LilyGO T-A7670x GNSS nie pochodzi
z modemu, tylko z **osobnego układu L76K**, i ten sam model jest sprzedawany
w dwóch wersjach, z nim i bez. Oferta bez wyraźnego „With GPS" to płytka bez GPS.

**Przy zamawianiu wymagać albo pełnego oznaczenia z sufiksem FASE, albo wyraźnej
wersji „with GPS", i sprawdzić złącze anteny GNSS na zdjęciu płytki.**

Efekt uboczny, i to dobry: L76K jest wielosystemowy (GPS, GLONASS, BeiDou),
podczas gdy NEO-6M jest wyłącznie GPS L1. To znaczy, że **ta zakupka rozwiązuje
otwarte pytanie z `00-poc.md` punkt 0.6** o to, czy NEO-6M gubi fix w mieście.
Nie trzeba już najpierw udowadniać, że jest za słaby: NEO-6M zostaje jako
redundancja i punkt odniesienia w pomiarach.

### Dwa konkretne zakupy, oba sprawdzone pod kątem GNSS

**Opcja 1, z polskiego sklepu, GNSS w modemie:**
[Waveshare ESP32-S3-A7670E-4G-EN, Kamami 1190055](https://kamami.pl/esp32/1190055-esp32-s3-a7670e-4g-development-board-lte-cat-1-2g-wifi-bluetooth-telephone-call-sms-gns-5906623486816.html),
193,69 zł, na stanie. Wykaz elementów producenta wymienia wprost **A7670E-FASE**
i osobne **złącze anteny GNSS (IPEX 1)**, a antena ceramiczna GNSS jest w zestawie.

- Za: GNSS siedzi w modemie, więc jeden UART mniej i ścieżka `MODEM_HAS_GNSS=1`
  zamiast osobnego odbiornika. Faktura, brak cła, dostawa krajowa.
- Przeciw: to **ESP32-S3**, a nie klasyczny ESP32, na który celuje `board =
  esp32dev`. Potrzebne nowe środowisko w `platformio.ini` i mapa pinów.
  Do tego kamera, głośnik, wejście panelu słonecznego i ładowarka ogniwa,
  czyli sporo rzeczy, których nie chcemy w budżecie prądowym.

**Opcja 2, klasyczny ESP32, GNSS z osobnego układu:**
[LilyGO T-A7670E R2 With GPS, wariant Q334](https://www.lilygo.cc/products/t-sim-a7670e),
32,89 USD u producenta, na stanie. To ten sam SKU, którego zdjęcie jest
w repozytorium LilyGO jako `Q334-T-A7670E-ESP32`.

- Za: ESP32-WROVER-E, czyli nasze obecne środowisko `lilygo_a7670` bez zmian.
  Mniej zbędnych układów na płytce.
- Przeciw: GNSS z L76K na osobnym UART. Wysyłka z Chin, doliczyć czas i cło.
  W UE ten sam model bywa u pośredników drożej, na przykład
  [OpenELAB, 44,95 EUR](https://openelab.io/products/lilygo-t-a7670e-r2-wireless-module).

Uwaga na oferty europejskie z wariantem **G**, na przykład
[botnroll Q425](https://www.botnroll.com/en/esp32/5053-t-a7670g-r2-sim-4g-lte-cat1-gsm-gprs-with-gps-l76k-esp32-wrover-e-18650-battery-holder-lilygo-q425.html):
mają GPS, ale to wariant pasmowy, dla którego nie ma potwierdzonej listy pasm.

### Czego ta płytka nie załatwia

- **Budżetu prądowego.** To jest płytka rozwojowa z układem USB-serial i PMU.
  `[DO ZMIERZENIA po zakupie: pobór w deep sleep, docs/04 punkt 4.2.]` Deklaracje
  producenta dotyczą modemu, nie całej płytki.
- **Ładowarki ogniwa.** Płytka ma uchwyt 18650 i ładowarkę. W aucie stojącym latem
  na słońcu ogniwo litowo-jonowe to ryzyko, którego nie chcemy. **Nie wkładać
  ogniwa**, zasilanie idzie z toru z `04` punkt 4.3.
- **Anten.** Potrzebne dwie: LTE i GNSS, obie na podszybie, nie w kokpicie
  (pozycje 10 i 11 w BOM z 3.7). Część ofert dokłada je w zestawie, część nie.
- **Karty SIM.** Osobna decyzja, nie ruszana w tym punkcie.

Firmware jest gotowy: `pio run -e lilygo_a7670`, a blok pinów
`BOARD_LILYGO_TA7670` jest w `include/pins.h` z adnotacją, że LilyGO zmienia
mapowanie między rewizjami i trzeba je potwierdzić na konkretnej sztuce.

## 3.8 BOM fazy 2 (odczyt CAN)

Dokupka wyłącznie do rozdziału 06. Wchodzi dopiero po zamknięciu PoC i po
30 dniach pracy v1 w ND1, nie wcześniej.

| Poz. | Element | Ilość | Po co |
|---|---|---|---|
| 13 | **SN65HVD231D** (SOIC-8, marking VP231) | 2 | transceiver docelowy, tryb sleep 40 nA |
| 14 | **SN65HVD230D** (SOIC-8, marking VP230) | 2 | transceiver na etap K2, tryb standby wyłącza nadajnik sprzętowo |
| 15 | Przejściówka SOIC-8 na DIP albo gotowy moduł breakout | 4 | montaż na płytce stykowej na czas K1-K3 |

Gotowy moduł zdejmuje robotę z lutowaniem i do nasłuchu wystarcza.
**Waveshare SN65HVD230 CAN Board** jest zweryfikowany ze schematu producenta
(`CAN_board.SchDoc`, 2011-09-05), więc poniżej nie ma domysłów:

| Element na płytce | Co z tego wynika |
|---|---|
| Goldpiny: cztery sygnały, `CAN_TX`, `GND`, `3.3V`, `CAN_RX` | wszystko, czego potrzeba do nasłuchu, jest na listwie |
| **R_S przez R1 10 kΩ do masy, bez wyprowadzenia** | tryb slope control na stałe. Standby i sleep **niedostępne bez przeróbki** |
| **R2 120 Ω między CANH a CANL, wlutowany, bez zwora** | **musi zostać wylutowany** przed podpięciem do auta |
| Złącze CAN: dwa piny, CANL i CANH | do wtyku OBD wystarczy |

Dwa wnioski praktyczne:

1. **Rezystor R2 wychodzi.** Nie ma zwora, jest wlutowany na stałe. Równolegle do
   terminacji auta daje 60 Ω i psuje komunikację w całym samochodzie, nie tylko
   u nas. Po wylutowaniu multimetr między CANH a CANL ma pokazać rozwarcie.
2. **R_S nie jest dostępny**, więc gwarancji „nadajnik wyłączony sprzętowo" nie
   da się tu zrobić przez standby. Daje ją za to pin `CAN_TX` na listwie
   zwarty do `3.3V` zamiast do GPIO: wejście nadajnika jest recesywne przy stanie
   wysokim, więc stopień wyjściowy nie ma jak ściągnąć magistrali w dół (patrz
   `06` punkt 6.2). Na tej płytce to jest zwykła zworka między dwoma sąsiednimi
   sygnałami listwy.

R1 10 kΩ ustawia slew rate około 15 V/µs. Przy 500 kb/s bit trwa 2 µs, więc to
nie ogranicza. Odbiornik w trybie slope control pracuje normalnie.

Do wersji docelowej z trybem sleep 40 nA ta płytka się nie nadaje bez przeróbki:
trzeba albo przylutować się do nóżki 8 układu i usunąć R1, albo wziąć sam układ
z pozycji 13 na przejściówce. Na etap K1-K3 to nie ma znaczenia.

Waveshare ostrzega na swoim wiki, że w obiegu są kopie tej płytki. Klony
(CJMCU-230 i to samo pod markami sprzedawców) mają zwykle ten sam układ
połączeń, ale tego nie zweryfikowałem i przy nich obie kontrole powyżej trzeba
zrobić multimetrem, a nie odczytać ze schematu.
| 16 | Load switch P-MOSFET (jak poz. 6) | 1 | trzeci egzemplarz, na szynę transceivera |
| 17 | Przewód OBD-II męski na luźne żyły, wszystkie 16 pinów | 1 | dostęp do 6/14 i 3/11 bez rozbierania wtyku z poz. 1 |

### Dlaczego SN65HVD231, a nie TJA1051

Liczby z kart katalogowych, nie z pamięci:

| | SN65HVD230 / 231 | TJA1051T/3 |
|---|---|---|
| Zasilanie V_CC | **3,0-3,6 V** | **4,5-5,5 V** |
| Logika 3,3 V | natywnie | przez osobny pin V_IO (2,8-5,5 V) |
| Tryb oszczędny | 230: standby 370 µA (nadajnik off, odbiornik **działa**) 231: sleep 40 nA (oba off) | silent 0,1-2,5 mA, typ. 1 mA (nadajnik off, odbiornik działa) |
| Kwalifikacja motoryzacyjna | karta katalogowa jej nie deklaruje | **AEC-Q100** |
| Prędkość | do 1 Mb/s | do 5 Mb/s (CAN FD) |

Rozstrzyga zasilanie: TJA1051 potrzebuje 5 V, a w naszym torze z 4.3 nie ma szyny
5 V i dokładanie jej tylko dla transceivera to kolejna przetwornica w budżecie
prądowym, który i tak jest napięty. SN65HVD231 siada na szynie 3,3 V, która już
jest, i jego tryb sleep to 40 nA, czyli **cztery rzędy wielkości mniej niż silent
w TJA1051**. Przy budżecie 400 µA na cały stan PARKED z 4.2 milliamper na samym
transceiverze jest nie do przyjęcia.

Cena za to: karta katalogowa SN65HVD23x nie deklaruje AEC-Q100 i podaje zakres
pracy tylko -40 do 85 °C. Pod deską rozdzielczą to wystarcza; gdyby układ miał
iść w komorę silnika, wybór byłby inny.

**Dwa typy, nie jeden**, bo mają różne tryby oszczędne i oba są nam potrzebne
w innym momencie:

- **SN65HVD230 na etap K2.** Pin R_S podciągnięty do V_CC włącza standby: nadajnik
  jest wyłączony sprzętowo, odbiornik pracuje dalej. To jest listen only wymuszone
  rezystorem, niezależne od tego, czy firmware poprawnie ustawił tryb TWAI. Przy
  pierwszym podpięciu do cudzej magistrali to jest dokładnie ta gwarancja, której
  chcemy, i kosztuje 370 µA, których na etapie zrzutów nikt nie liczy.
- **SN65HVD231 do wersji docelowej.** Ten sam pin R_S przy V_CC usypia i nadajnik,
  i odbiornik, przy 40 nA. Na postoju to jest zero w naszym bilansie.

Oba są w SOIC-8 z tym samym wyprowadzeniem, więc zamiana to wylutowanie jednego
i wlutowanie drugiego, bez zmian w płytce.

Load switch z pozycji 16 zostaje mimo trybu sleep. 40 nA to wartość typowa, nie
maksymalna, a poza tym sleep zależy od poprawnego stanu pinu R_S, czyli od
firmware. Brak zasilania nie zależy od niczego.

### Czego NIE trzeba kupować

- **Oscyloskopu ani analizatora stanów logicznych do K1.** Ustalenie, która para
  pinów ma magistralę 500 kb/s, robi się transceiverem, który i tak kupujemy:
  w trybie listen only kontroler nie wystawia na magistralę nic, nawet przy źle
  dobranej prędkości, więc wystarczy spróbować 500 kb/s na 6/14 i zobaczyć, czy
  lecą ramki bez błędów. Zła prędkość daje błędy odbioru u nas i ciszę na wodzie.
- **Rezystora terminującego 120 Ω.** Wręcz przeciwnie, patrz 06 punkt 6.2.
- **Izolowanego ISO1050.** Droższy, wymaga osobnego zasilania po drugiej stronie
  bariery i dokłada własny pobór. Do ND1 nieuzasadniony.
- **Gotowego interfejsu OBD (ELM327, adapterów BT/WiFi).** Nie pasuje do
  architektury: te układy są zaprojektowane do odpytywania, a my chcemy nasłuchu,
  i nie da się ich odciąć od magistrali na czas postoju.

## Źródła

- [Waveshare SIM7670G LTE Cat-1/GNSS HAT wiki](https://www.waveshare.com/wiki/SIM7670G_LTE_Cat-1/GNSS_HAT)
- [SIMCom A7670 Series Overview v2020.02, tabela pasm](https://make.net.za/wp-content/datasheets/SIMCom%20A7670%20Series%20Overview%20v2020.02.pdf)
- [LilyGO T-A7670G/E/SA R2, oferta producenta z wyborem wersji GPS](https://www.aliexpress.com/item/1005003036514769.html)
- [SN65HVD230/231/232, karta katalogowa TI SLOS346O](https://www.ti.com/lit/ds/symlink/sn65hvd230.pdf)
- [TJA1051, karta katalogowa NXP rev. 9](https://www.nxp.com/docs/en/data-sheet/TJA1051.pdf)
- [Waveshare SN65HVD230 CAN Board, schemat](https://files.waveshare.com/upload/c/c5/SN65HVD230-CAN-Board-Schematic.pdf)
- [Waveshare SN65HVD230 CAN Board, wiki](https://www.waveshare.com/wiki/SN65HVD230_CAN_Board)
- [SIM7670G 4G LTE Cat 1 Module User Manual](https://manuals.plus/ae/1005006673400672)
- [SIMCom: nowa generacja modułów LPWA SIM7070G/SIM7080G](https://www.simcom.com/news_view-38.html)
- [Ineltek: SIM7070G/SIM7080G, różnice w obsłudze 2G/E-GPRS](https://www.ineltek.com/en/lpwa-new-product-simcom-launches-its-new-generation-lpwa-module-solution-sim7070g-sim7080g/)
- [SIMCom A7670 Series specyfikacja](https://www.scribd.com/document/743712099/A7670-Series-SPEC-20200527)
- [SIMCOM A7670/A7672/A7676 Series LTE CAT1 Module User Manual](https://manuals.plus/ae/1005009816282133)
- [Porównanie NEO-6M, NEO-M8N i NEO-M9N](https://zbotic.in/gps-accuracy-comparison-neo-6m-vs-neo-m8n-vs-neo-m9n/)
- [FPV GPS module selection: M10 vs M8 chipset (2026)](https://blog.uavmodel.com/fpv-gps-module-selection-m10-vs-m8-chipset-baud-rate-and-mounting-best-practices-2026/)
