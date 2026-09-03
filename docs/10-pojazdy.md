# 10. Auta i konfiguracje per pojazd

Firmware jest jeden (Z6). Różnice siedzą w konfiguracji NVS i w profilu w HA.

**Od 2026-09-02 są trzy auta**, nie dwa: kupione trzy sztuki
LilyGO T-A7670E R2 with GPS. Trzecie to Mazda MX-5 **NBFL**, opisana w 10.3.

Nic w kodzie nie zakłada dwóch. Sprawdzone: `vehicle_id`, użytkownik i hasło
MQTT są ustawieniami NVS edytowanymi w portalu (`13`), a nie `#define`;
agregator ogłasza w discovery każdy pojazd, o którym się dowie, bez listy
w konfiguracji. Dwójka siedzi wyłącznie w tekście tego rozdziału i w liczbie
kont na brokerze.

## 10.1 ND1 2016

| Cecha | Wpływ na projekt |
|---|---|
| Auto sezonowe, długie postoje | Profil oszczędny: `int_park` 3600 s, wcześniejsza hibernacja, priorytet dla ochrony akumulatora |
| Akumulator po latach ma mniejszą pojemność użytkową | Progi napięciowe podniesione względem ND3: `v_hib` 12,0 V zamiast 11,9 V |
| Auto starsze i tańsze | Faza 2 z odczytem CAN wchodzi najpierw tutaj, nie w ND3 |
| Starsza elektronika | `[DO ZMIERZENIA: czy pin 16 gaśnie po zaśnięciu auta]` |

Główna wartość trackera w tym aucie to nie mapa, tylko **wykres napięcia akumulatora**
i alarm ruchu. Auto stojące zimą pod plandeką ma być widoczne w HA jako zdrowe albo nie.

## 10.2 ND3 2025

| Cecha | Wpływ na projekt |
|---|---|
| Auto bieżące, codzienna jazda | Profil pełny: historia przejazdów, geofence, statystyki |
| Auto nowe i wartościowsze | **Zero ingerencji w magistralę.** Faza 2 dopiero po ND1 i tylko w trybie pasywnego nasłuchu |
| Nowsza elektronika i moduły z własnym zarządzaniem energią | Auto zasypia głębiej, więc nasz pobór jest bardziej widoczny w bilansie. Kryterium 2 mA z 1.7 dotyczy przede wszystkim tego auta |
| Systemy wspomagania i większa liczba modułów | `[DO ZMIERZENIA: pobór spoczynkowy auta przed montażem trackera, żeby mieć punkt odniesienia]` |

### Gwarancja: niesprawdzone

Pierwsza wersja tego rozdziału podawała, że ND3 jest na gwarancji producenta,
a ND1 nie. **Nikt tego nie sprawdził**, to była inferencja z roczników, i została
stąd usunięta. `[DO USTALENIA: czy ND3 jest objęty gwarancją producenta, do kiedy,
i czy warunki mówią cokolwiek o urządzeniach wpinanych w gniazdo OBD.]`

Kolejność wdrożenia i tak się nie zmienia, bo nie opierała się na gwarancji:
ND1 jest starszy, tańszy, stoi sezonowo i jego elektronika jest lepiej opisana
przez społeczność. To wystarczy, żeby eksperymentować najpierw tam. Gdyby jednak
gwarancja realnie obowiązywała, dochodzi drugi argument i wtedy trzeba go zapisać
tutaj z konkretną datą końca, a nie z domysłu.

## 10.3 MX-5 NBFL

Proponowany `vehicle_id`: **`nbfl`**, w tej samej konwencji co `nd1` i `nd3`.

| Cecha | Wpływ na projekt |
|---|---|
| Rocznik z przełomu wieków, elektronika sprzed CAN | **Faza 2 z rozdziału 06 tego auta nie dotyczy.** Szczegóły niżej |
| Najstarszy akumulator w stawce | Progi jak w ND1 albo wyżej, `[DO USTALENIA: czy auto stoi sezonowo]` |
| Brak i-stop i prostszy alternator | Detekcja jazdy z napięcia powinna być czystsza niż w ND3 |
| Najstarsza instalacja | `[DO ZMIERZENIA: czy pin 16 jest stale zasilany i czy nie gaśnie, W3]` |

### Dlaczego CAN tu prawdopodobnie nie zadziała

Rozdział 06 opisuje magistralę CAN i transceiver SN65HVD230. To dotyczy ND1
i ND3. NBFL jest starszy niż obowiązek CAN w OBD-II.

Co wiadomo na pewno: EOBD jest obowiązkowe w UE dla nowych aut benzynowych od
1 stycznia 2001, więc gniazdo J1962 w tym aucie jest. Ale CAN po OBD
(ISO 15765-4) stał się obowiązkowy dopiero dla roczników mniej więcej od 2008.
Wcześniej normą było ISO 9141-2 albo ISO 14230-4 (KWP2000) na linii K, pin 7,
10,4 kb/s.

`[NIESPRAWDZONE u źródła: nie znalazłem dokumentu producenta ani serwisowego,
który wprost podaje protokół dla MX-5 NB. Nie przyjmować tego z tego akapitu.]`

**Rozstrzyga się to bez narzędzi, patrząc w gniazdo:**

| Co jest obsadzone | Protokół |
|---|---|
| piny 6 i 14 | CAN, plan z 06 działa bez zmian |
| pin 7 (i czasem 15) | linia K, plan z 06 **nie działa** |

### Co z tego wynika, jeżeli to linia K

Różnica jest głębsza niż inny układ scalony. **Na linii K nie ma czego
nasłuchiwać.** ISO 9141 i KWP2000 to protokoły pytanie-odpowiedź; sterownik nie
rozgłasza niczego z siebie. Cała konstrukcja z 06, oparta na pasywnym nasłuchu
ramek rozgłaszanych cyklicznie, w tym aucie nie ma zastosowania.

Zostaje wyłącznie odpytywanie, a odpytywanie przy zgaszonym silniku jest
dokładnie tym, czego 06 punkt 6.4 zabrania. Czyli w NBFL dane z auta są możliwe
**tylko przy pracującym silniku i tylko na żądanie**, albo wcale.

Sprzętowo linia K to nie jest ten sam świat co CAN: jedna żyła, poziomy 12 V,
potrzebny układ typu L9637D albo równoważny, nie transceiver CAN.

**Decyzja na teraz: w NBFL montujemy v1, czyli GPS, napięcie i alarm ruchu.**
Dane z auta są osobnym tematem, do otwarcia dopiero po sprawdzeniu gniazda
i po zamknięciu fazy 2 na ND1.

## 10.4 Konfiguracja per auto

Wartości poza domyślnymi, wysyłane retained na `cartracker/<id>/cfg`:

| Klucz | ND1 | ND3 | NBFL | Uzasadnienie |
|---|---|---|---|---|
| `int_park` | 3600 | 1800 | `[?]` | ND1 ma być cichy, ND3 bardziej responsywny; NBFL zależy od tego, czy stoi sezonowo |
| `v_hib` | 12.0 | 11.9 | 12.0 | starszy akumulator, wcześniejsze odcięcie |
| `v_warn` | 12.4 | 12.2 | 12.4 | wcześniejsze ostrzeżenie na aucie sezonowym |
| `int_alarm` | 15 | 15 | 15 | alarm ruchu jednakowo pilny wszędzie |
| `gnss_src` | `auto` | `auto` | `auto` | porównanie NEO-6M i GNSS modemu |

## 10.5 Dołożenie kolejnego pojazdu

Lista jest tu, bo dotąd ta wiedza była rozsypana po pięciu rozdziałach.

| Krok | Gdzie | Uwaga |
|---|---|---|
| 1. Nadać `vehicle_id` | portal urządzenia, sekcja Pojazd (`13`) | tworzy temat MQTT, litery, cyfry, `-` i `_` |
| 2. Wpisać rejestrację i VIN | tam samo | idą w `info`, podpisują auto na stronie floty i w HA |
| 3. Założyć konto MQTT `cartracker-<id>` | EMQX | jedno konto na pojazd, patrz `09` punkt 9.2. Hasło do menedżera haseł |
| 4. Wpisać konto w portalu | sekcja MQTT | nie w `config.h`, to są ustawienia fabryczne |
| 5. Profil konfiguracji | retained `cfg`, tabela 10.3 | interwały i progi napięcia zależą od tego, czy auto jest sezonowe |
| 6. Pomiary W1-W3 na tym aucie | `11` faza 1 | osobno dla każdego auta, patrz 10.5 |
| 7. Wpis `places` w HA | `08` punkt 8.1 | adres zamiast współrzędnych |

Kroki, których **nie ma** na tej liście, bo dzieją się same: encje w HA (hub
ogłasza nowy pojazd, gdy tylko przyjdzie od niego `info` albo `tel`), wiersz na
stronie floty, baza w agregatorze.

## 10.6 Czego nie zakładamy

Nie zakładamy, że auta mają identyczne gniazdo OBD, identyczne zachowanie pinu 16
ani identyczny profil napięcia z alternatora. Każdy z tych punktów jest w planie wdrożenia
(rozdział 11) jako osobny pomiar, wykonywany na każdym aucie niezależnie. Rocznik 2016
i rocznik 2025 to dziewięć lat różnicy w elektronice, mimo tej samej nazwy generacji,
a trzeci pojazd może nie być nawet Mazdą.
