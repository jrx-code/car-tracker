# 06. Odczyt danych z auta przez OBD-II (faza 2)

Faza 2: tracker przestaje być tylko odbiornikiem prądu z gniazda i zaczyna czytać
magistralę. Nadal nigdy nie wysyła ramki przy zamkniętym aucie. Ten rozdział opisuje
podpięcie, co realnie da się wyciągnąć z ND1 2016, i podział pracy na silnik
pracujący i zgaszony, bo to jest jedyna rzecz, która decyduje o tym, czy urządzenie
rozładuje akumulator.

Warunek wejścia bez zmian: v1 chodzi w aucie co najmniej miesiąc, a pomiar drainu
potwierdza budżet z `04-zasilanie-obd.md`.

## 6.1 Dwie drogi do danych, bierzemy obie, ale nie naraz

**Nasłuch pasywny.** Moduły auta rozgłaszają ramki cyklicznie, niezależnie od tego,
czy ktoś pyta. Transceiver w trybie `listen only` nie wystawia nawet bitu
potwierdzenia, więc auto nie wie o naszym istnieniu. Wada: ramki są własnością Mazdy
i trzeba je rozszyfrować samodzielnie.

**Zapytania OBD-II.** ISO 15765-4 na pinach 6 i 14 jest wymagane prawnie dla auta
z 2016 sprzedanego w UE, więc odpowiedzi na standardowe PID-y dostaniemy na pewno,
bez żadnego reverse engineeringu. Wada: zapytanie jest ramką wysłaną do auta, a przy
zgaszonym silniku wybudza moduły, które właśnie zasnęły.

Podział jest więc oczywisty i to on jest sednem tego rozdziału: **nasłuch zawsze,
zapytania tylko przy pracującym silniku.** Docelowo zapytania zostają wyłącznie do
DTC i do VIN-u, bo reszta i tak leci w rozgłoszeniach.

## 6.2 Podpięcie elektryczne

### Które piny

Tu jest sprzeczność między źródłami i nie wolno jej zamieść:

| Źródło | HS-CAN 500 kb/s | MS-CAN 125 kb/s | Czego dotyczy |
|---|---|---|---|
| mx5things (właściciel ND, sniffer w tym aucie) | piny 6 i 14 | piny 3 i 11 | **MX-5 ND** |
| Mazda6 Club | piny 3 i 11 | piny 6 i 14 | starsze Mazdy na platformie Forda |

Poprzednia wersja tego rozdziału cytowała tylko to drugie i sugerowała, że w ND
szybka magistrala jest na 3/11. Źródło ND-specyficzne mówi odwrotnie i zgadza się
z prawem: homologacja OBD-II wymaga, żeby magistrala diagnostyczna była na pinach
6 i 14. Auto z 2016 bez tego nie przeszłoby homologacji.

Wniosek roboczy: **piny 6 i 14 to HS-CAN 500 kb/s i to na nich pracujemy.**
Piny 3 i 11 to osobna magistrala, prawdopodobnie MS-CAN 125 kb/s, i w fazie 2
jej nie ruszamy. `[DO ZMIERZENIA na ND1 przed pierwszym podpięciem: analizatorem
albo oscyloskopem potwierdzić, która para ma ruch i z jaką prędkością. Nie
przyjmować tego z tabeli powyżej.]`

Pomiar jest tani: sonda na 6/14, druga na 3/11, zapłon włączony, patrzeć na
częstotliwość. Odróżnienie 500 kb/s od 125 kb/s widać gołym okiem na oscyloskopie.

### Tor sygnałowy

```
pin 6  (CAN H) --+
                 |   [odgalezienie, para nieprzerwana]
pin 14 (CAN L) --+
                 |
          [SN65HVD230 albo TJA1051T/3]  <-- Vcc przez load switch (PIN_CAN_EN)
                 |
          TWAI ESP32: PIN_CAN_TX 5, PIN_CAN_RX 18
```

Zasady, każda z konkretnego powodu:

- **Bez rezystora terminującego 120 Ω.** Magistrala auta jest już terminowana na obu
  końcach. Trzeci rezystor psuje impedancję i wywala komunikację w całym aucie, nie
  tylko u nas. Moduły SN65HVD230 z Aliexpress mają terminator wlutowany na płytce,
  trzeba go **wylutować albo przeciąć ścieżkę**.
- **Odgałęzienie, nie przerwanie pary.** Wtyk OBD daje dostęp równoległy, niczego
  nie przecinamy.
- **Load switch na zasilaniu transceivera.** Transceiver bez zasilania jest
  elektrycznie nieobecny na magistrali. To jest jedyny sposób, żeby mieć pewność,
  że przy zgaszonym silniku niczego nie robimy. Sam tryb standby układu nie
  wystarcza jako gwarancja, bo zależy od poprawnego stanu pinu sterującego, a load
  switch zależy od braku prądu.
- **GPIO 5 jest pinem strapującym ESP32** i musi być wysoki w chwili startu.
  Linia TX transceivera CAN spoczynkowo jest wysoka, więc to gra, ale gdyby
  transceiver ciągnął ją w dół przy starcie, ESP32 wejdzie w zły tryb bootowania.
  `[DO SPRAWDZENIA na stole: czy płytka wstaje z podłączonym transceiverem.]`
  Jeśli nie, przenieść TWAI TX na inny GPIO, bo TWAI daje się zmapować dowolnie.
- **Izolacja ISO1050** jest opcją, nie wymogiem. Droższa, ale odcina nasze błędy od
  magistrali auta. W ND3, jako w aucie nowszym i droższym, warta rozważenia.

## 6.3 Co da się odczytać z ND1 2016

### Ramki rozgłaszane, czyli to, co bierzemy pasywnie

Potwierdzone niezależnie przez dwa źródła (RaceChrono DIY na MX-5 ND 2019 oraz
wątek na mx5things), więc traktujemy jako punkt startu, nie jako pewnik dla
rocznika 2016:

| ID (hex) | ID (dec) | Sygnał | Wyliczenie |
|---|---|---|---|
| `0x202` | 514 | obroty silnika | `uint16(B0,B1) / 4` |
| `0x202` | 514 | prędkość | `uint16(B2,B3)` = km/h × 100 |
| `0x202` | 514 | pedał gazu | `B4 / 2,5` = procent |
| `0x078` | 120 | pedał hamulca | procent, wyciągany bitowo |
| `0x086` | 134 | kąt kierownicy | `(16000 - uint16(B0,B1)) × 0,1`, dodatnie w prawo |

Dwa źródła zgadzają się co do `0x202` po podstawieniu jednostek: surowa prędkość to
km/h × 100, RaceChrono dzieli przez 360, bo jego kanał jest w m/s, a
`(km/h × 100) / 360 = (km/h) / 3,6 = m/s`. To jest niezależne potwierdzenie, nie
przepisanie jednego źródła przez drugie.

**Publicznie nieudokumentowane dla ND:** poziom paliwa, temperatura płynu, bieg,
sprzęgło. Autor bazy RaceChrono wprost je wymienia jako nierozszyfrowane. To jest
robota do zrobienia u nas, opisana w K3 poniżej.

### Standardowe PID-y, czyli to, o co pytamy przy pracującym silniku

Tryb 01 po ISO 15765-4. Istnienie protokołu jest pewne, wsparcie konkretnego PID-u
już nie i ustala się je bitmaskami wsparcia (`0x00`, `0x20`, `0x40`, `0x60`),
a nie zgadywaniem:

| PID | Dana | Uwaga |
|---|---|---|
| `0x05` | temperatura płynu chłodzącego | `A - 40` °C |
| `0x0C` | obroty | dublują `0x202`, dobre do walidacji dekodera |
| `0x0D` | prędkość | jw. |
| `0x2F` | poziom paliwa | **procent, nie litry** |
| `0x1F` | czas od uruchomienia silnika | przydatne do liczenia przejazdu |
| `0x42` | napięcie sterownika | druga opinia obok naszego dzielnika z 4.4 |
| `0x04` | obciążenie silnika | |
| `0x0F` | temperatura powietrza dolotowego | |
| `0xA6` | przebieg całkowity | nowszy dodatek do J1979, `[DO SPRAWDZENIA czy ND1 2016 go wspiera]` |

Poza trybem 01:

- **Tryb 09 PID `0x02` to VIN.** Auto samo poda swój numer nadwozia. Pole `vin`
  jest już w ustawieniach urządzenia i w `info` (patrz `13-portal-konfiguracji.md`
  i `05-protokol-mqtt.md` 5.9), więc tracker może je wypełnić sam zamiast czekać,
  aż ktoś przepisze VIN z dowodu. Wpisujemy tylko wtedy, gdy pole jest puste, i
  nigdy nie nadpisujemy wartości ustawionej ręcznie: rozjazd między VIN-em z auta
  a wpisanym w portalu to informacja, że płytka trafiła do innego auta, i nie wolno
  jej zamazać.
- **Tryb 03, 07 i 0A**: kody usterek zapisane, oczekujące i trwałe. Tylko na żądanie
  z HA, nigdy w pętli.

Czego świadomie nie robimy: kasowania kodów (tryb 04). Urządzenie ma nigdy nie
zmieniać stanu auta. Kasowanie DTC gasi kontrolkę i zeruje dane gotowości, co przy
badaniu technicznym potrafi zaszkodzić bardziej niż sama usterka.

## 6.4 Plan na silnik pracujący i zgaszony

To jest właściwa treść fazy 2. Trzy stany, jeden przełącznik.

### Silnik pracuje (`DRIVING`)

Rozpoznanie po tym, co już mamy: napięcie powyżej `v_drive_on` **lub** trwały ruch
z akcelerometru (`02-architektura.md` 2.4). Nowego kryterium nie wprowadzamy.

1. Load switch podaje napięcie na transceiver.
2. TWAI startuje w **trybie normalnym**, 500 kb/s, z filtrem akceptacji ustawionym
   na te ID, które nas interesują. Filtr sprzętowy, nie w kodzie: przy pełnym ruchu
   na HS-CAN kolejka programowa zapycha się w sekundy.
3. Nasłuch daje obroty, prędkość i pedały na bieżąco.
4. Zapytania OBD idą **rzadko i pojedynczo**, nie w pętli:
   - poziom paliwa i temperatura płynu co 60 s,
   - VIN raz, przy pierwszym uruchomieniu z pustym polem,
   - DTC wyłącznie na komendę z HA.
5. Przejazd zamykany jest nadal po `trip_end` z detekcji napięcia i ruchu. CAN
   dokłada dane do przejazdu, ale nie decyduje o jego granicach. Jedna decyzja,
   jedno źródło.

### Silnik zgaszony (`PARKED`, `MOVED`, `HIBERNATE`)

1. **Load switch odcina zasilanie transceivera.** Nie standby, nie tryb uśpienia
   układu: brak napięcia.
2. TWAI zatrzymany, sterownik odinstalowany, piny w stanie wysokiej impedancji.
3. Zero zapytań. Zero ramek. Auto ma zasnąć i nie ma prawa nas zauważyć.
4. Ostatnie odczyty z CAN zostają w RAM i idą w kolejnej telemetrii jako wartości
   z ostatniej jazdy, ze znacznikiem czasu, żeby w HA nie wyglądały na bieżące.

Wyjątkiem nie jest nawet alarm holowania: `MOVED_WHILE_PARKED` nadaje pozycję
z GNSS i tyle. Wybudzanie magistrali w aucie, które właśnie ktoś zabiera, nie daje
żadnej informacji, której nie mamy z GPS-u, a kosztuje prąd i pozostawia ślad.

### Przejścia

| Przejście | Kolejność |
|---|---|
| `PARKED` -> `DRIVING` | najpierw stabilne kryterium jazdy przez 5 s, potem load switch, potem start TWAI. Nie odwrotnie: transceiver włączony na sekundę przy fałszywym wykryciu to ramka na magistrali. |
| `DRIVING` -> `PARKED` | najpierw stop TWAI, potem odcięcie zasilania, dopiero potem zmiana stanu i `trip_end`. Odcięcie zasilania działającemu kontrolerowi potrafi zostawić dominującą kolejkę na magistrali. |
| dowolny -> `HIBERNATE` | jak wyżej, plus twarde sprawdzenie, że load switch jest wyłączony, zanim ESP32 pójdzie spać. Deep sleep z zasilonym transceiverem to 10-20 mA płynące przez całą noc, czyli cały budżet z 4.2 w błoto. |

### Zabezpieczenie, które musi być w kodzie, nie w procedurze

Bezpiecznik czasowy: jeżeli transceiver jest zasilony dłużej niż `can_max_on_s`
(domyślnie 4 h) bez potwierdzonego kryterium jazdy, firmware go odcina i wysyła
zdarzenie `can_forced_off`. Ta sytuacja znaczy, że detekcja jazdy się zawiesiła,
a wtedy wolimy stracić dane niż akumulator.

## 6.5 Etapy

| Krok | Co | Kryterium zamknięcia |
|---|---|---|
| K1 | Pomiar, która para pinów ma jaki ruch i z jaką prędkością w ND1 | zapisane w tym pliku, tabela z 6.2 zastąpiona wynikiem |
| K2 | Transceiver na stole, `listen only`, zrzut ruchu z 15 min jazdy do pliku | zrzut istnieje, zero ramek wysłanych (potwierdzone drugim urządzeniem na magistrali) |
| K3 | Analiza offline: potwierdzić `0x202`, `0x078`, `0x086`, znaleźć paliwo, temperaturę i bieg | każdy sygnał potwierdzony na co najmniej trzech niezależnych przejazdach, nie na jednym |
| K4 | Zapytania trybu 01, bitmaski wsparcia, lista realnie obsługiwanych PID-ów w ND1 | tabela z 6.3 zastąpiona zmierzoną listą |
| K5 | Load switch i maszyna stanów z 6.4, pomiar poboru w `PARKED` z zamontowanym transceiverem | pobór nie wzrósł mierzalnie względem v1 |
| K6 | VIN z trybu 09 wpisywany do pustego pola ustawień | VIN z auta zgadza się z tabliczką |

K3 jest najdłuższy i nie da się go skrócić. Dopasowanie sygnału do jednej jazdy to
nie jest dopasowanie, tylko zbieg okoliczności; poziom paliwa zmienia się na tyle
wolno, że w jednym przejeździe wygląda jak dowolny inny wolno pełzający licznik.

## 6.6 Ryzyka

- **Terminator na module transceivera.** Najczęstszy sposób na zepsucie magistrali
  całego auta. Sprawdzić multimetrem przed podpięciem: między CAN H a CAN L
  odłączonego modułu ma być rozwarcie, a nie 120 Ω.
- **Prędkość magistrali dobrana źle.** Kontroler z niedopasowaną prędkością zgłasza
  błędy ramek i przy dużej liczbie błędów przechodzi w stan `bus off`, a po drodze
  wystawia bity błędu na magistralę. `listen only` chroni przed tym, więc do czasu
  potwierdzenia prędkości w K1 nie wychodzimy z tego trybu.
- **Odpytywanie przy zgaszonym silniku.** Powód, dla którego to nie weszło do v1,
  nie zniknął. Cała sekcja 6.4 istnieje po to, żeby nie dało się tego zrobić przez
  przypadek.
- **Kolejność aut.** Fazę 2 wdrażamy najpierw w ND1: starszy, tańszy, stoi
  sezonowo i ma lepiej opisaną przez społeczność elektronikę. Status gwarancji
  ND3 jest niesprawdzony i nie jest tu argumentem, patrz `10-pojazdy.md` 10.2.

## Źródła

- [RaceChrono DIY: baza CAN dla MX-5 ND (testowane na ND 2019)](https://github.com/timurrrr/RaceChronoDiyBleDevice/blob/master/can_db/mazda_mx5_nd.md)
- [mx5things: sniffer CAN w MX-5 ND, przypisanie pinów i pierwsze ID](https://mx5things.blog/2017/02/18/can-bus-sniffer/)
- [Madox.NET: reverse engineering magistrali CAN w Mazdach](http://www.madox.net/blog/2008/11/17/reverse-engineering-the-mazda-can-bus-part-1/)
- [Mazda6 Club: prędkości magistral HS/MS i przypisanie pinów w starszych Mazdach](https://www.mazda6club.com/threads/deciphering-the-can-bus.449116/)
- [Alison Chaiken: eksperymenty z CAN w Mazdzie 3](https://she-devel.com/Mazda3_Controller_Area_Network_Experimentation.html)
