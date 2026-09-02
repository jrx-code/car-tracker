# 10. Auta i konfiguracje per pojazd

Firmware jest jeden (Z6). Różnice siedzą w konfiguracji NVS i w profilu w HA.

**Od 2026-09-02 planowane są trzy auta**, nie dwa: kupione trzy sztuki
LilyGO T-A7670E R2 with GPS. Trzeci pojazd nie jest jeszcze opisany, bo nie
zapadła decyzja, które to auto. `[DO USTALENIA: marka, rocznik, identyfikator
pojazdu, czy sezonowe czy codzienne.]`

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

## 10.3 Konfiguracja per auto

Wartości poza domyślnymi, wysyłane retained na `cartracker/<id>/cfg`:

| Klucz | ND1 | ND3 | Uzasadnienie |
|---|---|---|---|
| `int_park` | 3600 | 1800 | ND1 ma być cichy, ND3 bardziej responsywny |
| `v_hib` | 12.0 | 11.9 | starszy akumulator, wcześniejsze odcięcie |
| `v_warn` | 12.4 | 12.2 | wcześniejsze ostrzeżenie na aucie sezonowym |
| `int_alarm` | 15 | 15 | alarm ruchu jednakowo pilny w obu |
| `gnss_src` | `auto` | `auto` | porównanie NEO-6M i GNSS modemu w obu autach |

## 10.4 Dołożenie kolejnego pojazdu

Lista jest tu, bo dotąd ta wiedza była rozsypana po pięciu rozdziałach.

| Krok | Gdzie | Uwaga |
|---|---|---|
| 1. Nadać `vehicle_id` | portal urządzenia, sekcja Pojazd (`13`) | tworzy temat MQTT, litery, cyfry, `-` i `_` |
| 2. Wpisać rejestrację i VIN | tam samo | idą w `info`, podpisują auto na stronie floty i w HA |
| 3. Założyć konto MQTT `cartracker-<id>` | EMQX | jedno konto na pojazd, patrz `09` punkt 9.2. Hasło do menedzer hasel |
| 4. Wpisać konto w portalu | sekcja MQTT | nie w `config.h`, to są ustawienia fabryczne |
| 5. Profil konfiguracji | retained `cfg`, tabela 10.3 | interwały i progi napięcia zależą od tego, czy auto jest sezonowe |
| 6. Pomiary W1-W3 na tym aucie | `11` faza 1 | osobno dla każdego auta, patrz 10.5 |
| 7. Wpis `places` w HA | `08` punkt 8.1 | adres zamiast współrzędnych |

Kroki, których **nie ma** na tej liście, bo dzieją się same: encje w HA (hub
ogłasza nowy pojazd, gdy tylko przyjdzie od niego `info` albo `tel`), wiersz na
stronie floty, baza w agregatorze.

## 10.5 Czego nie zakładamy

Nie zakładamy, że auta mają identyczne gniazdo OBD, identyczne zachowanie pinu 16
ani identyczny profil napięcia z alternatora. Każdy z tych punktów jest w planie wdrożenia
(rozdział 11) jako osobny pomiar, wykonywany na każdym aucie niezależnie. Rocznik 2016
i rocznik 2025 to dziewięć lat różnicy w elektronice, mimo tej samej nazwy generacji,
a trzeci pojazd może nie być nawet Mazdą.
