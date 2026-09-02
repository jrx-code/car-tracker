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

## Źródła

- [Waveshare SIM7670G LTE Cat-1/GNSS HAT wiki](https://www.waveshare.com/wiki/SIM7670G_LTE_Cat-1/GNSS_HAT)
- [SIM7670G 4G LTE Cat 1 Module User Manual](https://manuals.plus/ae/1005006673400672)
- [SIMCom: nowa generacja modułów LPWA SIM7070G/SIM7080G](https://www.simcom.com/news_view-38.html)
- [Ineltek: SIM7070G/SIM7080G, różnice w obsłudze 2G/E-GPRS](https://www.ineltek.com/en/lpwa-new-product-simcom-launches-its-new-generation-lpwa-module-solution-sim7070g-sim7080g/)
- [SIMCom A7670 Series specyfikacja](https://www.scribd.com/document/743712099/A7670-Series-SPEC-20200527)
- [SIMCOM A7670/A7672/A7676 Series LTE CAT1 Module User Manual](https://manuals.plus/ae/1005009816282133)
- [Porównanie NEO-6M, NEO-M8N i NEO-M9N](https://zbotic.in/gps-accuracy-comparison-neo-6m-vs-neo-m8n-vs-neo-m9n/)
- [FPV GPS module selection: M10 vs M8 chipset (2026)](https://blog.uavmodel.com/fpv-gps-module-selection-m10-vs-m8-chipset-baud-rate-and-mounting-best-practices-2026/)
