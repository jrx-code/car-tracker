# 04. Zasilanie z OBD-II

To najtrudniejsza część projektu. Nie dlatego, że trudno wziąć 12 V z gniazda, tylko
dlatego, że urządzenie wisi na akumulatorze auta na stałe i musi być niewidoczne
w bilansie prądowym.

## 4.1 Gniazdo OBD-II (SAE J1962)

Piny, które nas dotyczą:

| Pin | Sygnał | Użycie w projekcie |
|---|---|---|
| 4 | Chassis ground | masa zasilania |
| 5 | Signal ground | masa sygnałowa, w v1 zmostkowana z pinem 4 przy wtyku |
| 6 | CAN High | **niepodłączony w v1** |
| 14 | CAN Low | **niepodłączony w v1** |
| 16 | +12 V z akumulatora, stale zasilany | zasilanie trackera |

Zasilanie na pinie 16 jest z definicji standardu podpięte bezpośrednio do akumulatora,
czyli obecne przy wyłączonym zapłonie. Na tym opiera się cała koncepcja urządzenia
i to jest równocześnie źródło ryzyka rozładowania.

**Procedura weryfikacji przed pierwszym wpięciem, na każdym aucie osobno:**

1. Multimetr między pinem 16 a pinem 4 przy wyłączonym zapłonie: oczekiwane 12,0-12,7 V.
2. To samo przy pracującym silniku: oczekiwane 13,5-14,8 V. Zapisz wartość, bo z niej
   wynikają progi z 2.3.
3. Pomiar przy włączonym zapłonie bez rozruchu, żeby zobaczyć spadek na rozruszniku.
4. Sprawdź, czy pin 16 nie gaśnie po kilkunastu minutach od zamknięcia auta. Część
   producentów usypia gniazdo, żeby ograniczyć pobór. `[DO ZMIERZENIA na ND1 i ND3]`
   Jeżeli gaśnie, koncepcja stanu PARKED wymaga korekty, bo tracker traci zasilanie.
5. Zanotuj profil napięcia przy jeździe miejskiej z i-stop, jeśli auto go ma.

## 4.2 Budżet prądowy i dlaczego DevKit odpada

Zdrowy pobór spoczynkowy całego auta to rząd 25 mA. To jest punkt odniesienia:
nasze urządzenie ma być ułamkiem tej wartości, a nie jej wielokrotnością.

Typowa płytka ESP32 DevKit ma na pokładzie regulator liniowy i układ USB-serial.
Obie rzeczy pobierają prąd niezależnie od tego, czy ESP32 śpi.
`[DO ZMIERZENIA na Twojej konkretnej płytce: pobór przy ESP32 w deep sleep, mierzony na wejściu 5 V]`
Jeżeli wyjdzie kilkanaście mA, płytka nie nadaje się do stałego wpięcia w OBD w tej
formie i trzeba albo usunąć układ USB-serial i regulator, albo przejść na moduł
ESP32-WROOM na własnej płytce, albo na wariant D z rozdziału 03, gdzie zasilaniem
zarządza PMU.

Bilans docelowy w stanie PARKED:

| Element | Prąd | Uwaga |
|---|---|---|
| Przetwornica step-down w trybie pulse skip | około 140 µA | TPS54240 podaje 138 µA przy braku obciążenia w Eco-mode |
| ESP32 w deep sleep z RTC i przerwaniem GPIO | 10-150 µA | zależnie od modułu |
| Akcelerometr LIS3DH w trybie low power z detekcją ruchu | rząd 10 µA | to jest jego zastosowanie docelowe |
| Dzielnik napięcia do pomiaru 12 V | 21 µA przy 14 V | 560 kΩ / 100 kΩ, patrz 4.4 |
| Modem odcięty load switchem | około 0 | dlatego odcinamy, a nie usypiamy |
| GNSS odcięty load switchem | około 0 | tak samo |
| **Suma projektowa** | **poniżej 400 µA** | z zapasem mieści się w celu 2 mA z 1.7 |

W ciągu 8 tygodni 400 µA to około 0,54 Ah. To poniżej progu wykrywalności przy zwykłym
pomiarze drainu i nie ma wpływu na rozruch.

## 4.3 Tor zasilania

```
pin 16  --[bezpiecznik 500 mA]--+--[TVS SMBJ24A]--+--[ideal diode / Schottky]--+
                                |                 |                            |
pin 4/5 ------------------------+-----------------+----------------------------+-- GND
                                                                               |
                                                            [buck 42V in, 3,8V out, Iq ~140 µA]
                                                                               |
                    +----------------------------+----------------------------+
                    |                            |                            |
              [LDO 3,3 V dla ESP32]      [load switch] -> modem 3,8 V   [load switch] -> GNSS 3,3 V
```

Uzasadnienie elementów:

- **Bezpiecznik 500 mA** chroni instalację auta, nie tracker. Zwarcie w naszym urządzeniu
  nie może spalić przewodu w wiązce ani wywołać pożaru. To jest wymóg, nie opcja.
- **TVS 24 V** przyjmuje skoki przy rozruchu i load dump. Napięcie w instalacji auta
  nie jest równe 12 V, tylko jest 12 V z impulsami.
- **Ochrona odwrotnej polaryzacji** na wypadek pomyłki przy budowie i testach na stole.
- **Buck 42 V wejściowe** daje zapas na przepięcia. TPS54240-Q1 jest kwalifikowany
  AEC-Q100, dopuszcza 42 V wejścia i 2,5 A, a producent wprost wskazuje moduły GSM/GPRS
  w telematyce jako aplikację docelową.
- **3,8 V jako szyna główna** dlatego, że modemy SIMCom chcą 3,4-4,2 V. ESP32 dostaje
  3,3 V z małego LDO za szyną 3,8 V, co zdejmuje z niego szpilki prądowe modemu.
- **Kondensator 470-1000 µF przy module modemu.** Bez niego rejestracja w sieci potrafi
  zapadać napięcie i resetować ESP32. To jest najczęstsza przyczyna "modem nie działa"
  w tego typu konstrukcjach.

## 4.4 Pomiar napięcia akumulatora

Dzielnik z pinu 16 na ADC ESP32, zaprojektowany tak, żeby sam nie był obciążeniem.
Kondensator 100 nF równolegle do dolnego rezystora filtruje szpilki. ADC ESP32 jest
nieliniowy, więc firmware używa kalibracji dwupunktowej zapisanej w NVS, a nie wzoru
teoretycznego. Procedura kalibracji jest w `docs/11-plan-wdrozenia.md`, krok W4.

### Dobór rezystorów: 560 kΩ / 100 kΩ

Pierwsza wersja tego punktu podawała 470 kΩ / 100 kΩ. **To jest za mało i psuje
dokładnie ten zakres, na którym nam zależy.** ESP-IDF podaje dla tłumienia 11 dB
zalecany zakres wejściowy 150-2450 mV, a powyżej niego charakterystyka spłaszcza
się, bo ogranicza ją VDD_A, nie pełna skala przetwornika.

Dzielnik 470/100 ma przekładnię 0,1754, więc:

| Napięcie na pinie 16 | Na ADC przy 470/100 | Na ADC przy 560/100 |
|---|---|---|
| 11,5 V (bliskie hibernacji) | 2,018 V | 1,742 V |
| 12,7 V (postój, akumulator zdrowy) | 2,228 V | 1,924 V |
| 13,5 V (dolna granica pracy silnika) | 2,368 V | 2,045 V |
| **14,8 V (górna granica pracy silnika)** | **2,596 V, poza zakresem** | 2,242 V |
| 16,0 V | 2,807 V, poza zakresem | 2,424 V |

Przy 470/100 cały przedział pracy silnika z 4.1, czyli 13,5-14,8 V, leży na
granicy albo już poza zalecanym zakresem. To jest właśnie ten przedział, po
którym rozpoznajemy jazdę progiem `v_drive_on` z 2.3, więc spłaszczenie
charakterystyki uderza dokładnie w decyzję, do której ten pomiar służy.
Kalibracja dwupunktowa tego nie ratuje, bo problem nie polega na przesunięciu
prostej, tylko na tym, że powyżej 2,45 V nie ma już prostej.

560 kΩ / 100 kΩ mieści cały zakres 11-16 V wewnątrz 150-2450 mV, a przy okazji
pobiera mniej: 21 µA przy 14 V zamiast 25 µA.

Czego to nadal nie mierzy: skoków powyżej 16 V. Dzielnik wtedy saturuje i pokaże
sufit. To jest zamierzone, bo od load dumpu jest TVS z 4.3, a nie przetwornik.
Firmware ma traktować odczyt przy suficie jako brak pomiaru, nie jako 16 V.

`[DO ZMIERZENIA W2: rzeczywisty profil napięcia w ND1. Jeżeli alternator ND
wyjdzie poza 16 V, dolny rezystor trzeba jeszcze zmniejszyć.]`

## 4.5 Ochrona akumulatora (odcięcie podnapięciowe)

Progi domyślne, konfigurowalne z HA:

| Próg | Wartość | Działanie |
|---|---|---|
| Ostrzeżenie | 12,2 V przez 30 min | pakiet z flagą `battery_low`, powiadomienie w HA |
| Hibernacja | 11,9 V przez 10 min | tracker wysyła ostatni pakiet i zasypia na 6 h |
| Wyjście z hibernacji | 12,4 V | powrót do PARKED |

Dolny próg nie jest dobrany do tego, żeby chronić tracker, tylko do tego, żeby zostawić
akumulatorowi zapas na rozruch. Auto ma odpalić nawet jeśli oznacza to, że tracker milczy.
To wynika wprost z założenia Z2.

## 4.6 Ryzyka specyficzne dla MX-5 ND

Na forach właścicieli ND opisane są przypadki nadmiernego poboru spoczynkowego
pochodzącego z samego auta: obwód audio na jednym z bezpieczników, moduł ABS klikający
cyklicznie i pobierający około 1 A przez kilka sekund, oraz zwiększony pobór przy
zostawionym aktywnym tempomacie w wersji RF. To istotne z dwóch powodów.

Po pierwsze, jeżeli po zamontowaniu trackera akumulator zacznie padać, pierwszym
podejrzanym będzie tracker, a przyczyna może być inna. Dlatego krok W1 planu wdrożenia
to **pomiar drainu auta przed montażem**, żeby mieć punkt odniesienia.

Po drugie, tracker mierzący napięcie co godzinę jest dobrym narzędziem do wykrycia
takiej usterki: w HA widać profil rozładowania i można odróżnić powolne samorozładowanie
od skokowego poboru.

## Źródła

- [ESP-IDF: zakresy wejściowe ADC dla poszczególnych tłumień](https://docs.espressif.com/projects/esp-idf/en/v4.4/esp32/api-reference/peripherals/adc.html)
- [TPS54240 42-V 2,5-A step-down regulator, TI](https://www.ti.com/product/TPS54240)
- [TPS54240 datasheet (Eco-mode, 138 µA)](https://www.ti.com/lit/ds/symlink/tps54240.pdf)
- [TPS54240-Q1, wersja motoryzacyjna AEC-Q100](https://www.ti.com/product/TPS54240-Q1)
- [MX-5 OC forum: pobór spoczynkowy ND z bezpiecznika 17 (audio)](https://forum.mx5oc.co.uk/t/mx5-nd-fixed-parasitic-battery-drain-from-fuse17-audio/147195)
- [PistonHeads: ND RF, rozładowanie akumulatora na postoju](https://www.pistonheads.com/gassing/topic.asp?h=0&f=185&t=1710223)
- [JustAnswer: ND 1.5, pobór od modułu ABS](https://www.justanswer.co.uk/mazda/lm871-mazda-mx5-nd-1-5-parasitic-drain.html)
- [Drivetrain Resource: diagnostyka rozładowania w MX-5, próg 25 mA](https://www.700r4transmissionhq.com/mazda-mx-5-battery-draining/)
