# 08. Integracja Home Assistant

Katalog `ha-integration/custom_components/car_tracker`. Docelowo HA 2026.8.3
(wersja sprawdzona na prodzie), integracja typu `local_push`, zależna od `mqtt`.

## 8.1 Instalacja

**Nic się nie instaluje.** Encje powstają z MQTT discovery publikowanego przez
`tracker-hub` (uzasadnienie w 8.3), więc wystarczy, że HA ma skonfigurowaną
integrację MQTT wskazującą na tego samego brokera. Konfiguracje są retained,
więc pojawiają się same przy pierwszym połączeniu.

Nowy tracker też pojawia się sam: hub ogłasza każdy pojazd, o którym dowie się
z tematu `info` albo `tel`.

Uwaga na wspólnego brokera: retained konfiguracje discovery widzi **każdy** HA
podpięty do EMQX, także produkcyjny. Jeżeli testowa instancja ma nie mieszać
prodowi, trzeba zmienić `HA_DISCOVERY_PREFIX` w hubie, a nie liczyć na to, że
prod tego nie zobaczy.

### Adres zamiast współrzędnych

Encja `device_tracker.<auto>` podpięta pod integrację `places` daje sensor
z adresem, tak samo jak dla osób. Konfiguracja skopiowana z wpisu „Osoba":
`options: zone, place`, `map_provider: osm`, `language: pl`,
`home_zone: zone.home`, `use_gps_accuracy: true`. W strefie encja pokazuje
nazwę strefy, adres dopiero poza nią.

## 8.2 Encje

Jedno urządzenie na pojazd, encje z `has_entity_name`.

| Encja | Typ | Uwagi |
|---|---|---|
| `device_tracker.<auto>` | GPS | pozycja, dokładność wyliczana z HDOP |
| Napięcie akumulatora | sensor, V | to jest najważniejsza encja dla ND1 |
| Prędkość | sensor, km/h | z GNSS, nie z auta |
| Satelity, HDOP, siła sygnału, sieć | sensor, diagnostyka | |
| Punkty w kolejce | sensor, diagnostyka | rośnie, gdy łącze siada. Pierwszy sygnał ostrzegawczy |
| Tryb | sensor enum | postój, jazda, ruch na postoju, hibernacja |
| Dystans bieżącego przejazdu | sensor, km | liczony w HA z pozycji |
| Ostatni przejazd: dystans, czas, prędkość maks. | sensor | zostaje po zakończeniu jazdy |
| Jazda | binary_sensor moving | |
| Alarm ruchu | binary_sensor problem | decyzja urządzenia, nie HA |
| Niskie napięcie | binary_sensor battery | próg 12,2 V |
| Hibernacja, roaming | binary_sensor, diagnostyka | |

## 8.3 MQTT discovery czy własna integracja

Pierwsza wersja tego punktu miała cztery powody, dla których to jest integracja,
a nie discovery: deduplikacja po `seq`, filtr niemożliwych skoków, przejazdy
i konfiguracja z UI. **Trzy z tych czterech przejął `tracker-hub`**, który
powstał później, jest wdrożony i chodzi. Punkt trzeba więc przeliczyć od nowa,
bo uzasadnienie z niego wyparowało po cichu.

### Kto co dzisiaj robi

| Zadanie | firmware | tracker-hub | integracja HA |
|---|---|---|---|
| Deduplikacja po `seq` | nie | tak, unikalny indeks `(vehicle_id, seq)` | tak, `seen_seq` |
| Odrzucanie HDOP > 5,0 | tak | tak | tak |
| Filtr skoku ponad 300 km/h | nie | tak | tak |
| Przejazdy: dystans, maks., średnia | nie | tak, tabela `trips` | tak, `Trip` |
| Historia | kolejka offline | SQLite, retencja 180 dni | recorder |
| Konfiguracja urządzenia | NVS | `POST /api/vehicles/<id>/config` | opcje integracji |

Progi są dziś zgodne: 300 km/h i HDOP 5,0 po obu stronach. To nie jest zasługa
mechanizmu, tylko tego, że jedno przepisano z drugiego, i nic nie pilnuje, żeby
tak zostało.

Gorsze jest to, że **format wiadomości ma dziś trzy implementacje**:
`packet.cpp` pisze, `ingest.py` czyta, `coordinator.py` czyta drugi raz. Każda
ma własną kopię mapy skróconych kluczy. `CLAUDE.md` przypomina o dwóch z nich.

### Co discovery potrafi, a czego nie

Potrafi wszystko z tabeli 8.2: `sensor`, `binary_sensor`, `device_tracker`,
grupowanie w jedno urządzenie, kategorie diagnostyczne, dostępność z LWT.
Do komend `button` z `command_topic`, do progów napięcia `number`. Te ostatnie
działają, bo `applyConfig` w firmware scala klucze zamiast zastępować całość,
a `saveCfg()` odkłada wynik do NVS, więc publikacja pojedynczego klucza nie gubi
pozostałych.

Nie potrafi jednej rzeczy: **pamiętać poprzedniej wiadomości**. Szablon jest
bezstanowy, więc deduplikacja, filtr teleportu i naliczanie dystansu są poza
jego zasięgiem. Cała różnica sprowadza się do tego jednego zdania.

### Gdzie ma mieszkać stan

Skoro logika stanowa musi gdzieś być, to pytanie brzmi gdzie, a nie czy.

- **W firmware**: odpada, `02` punkt 2.8 mówi wprost, że urządzenie ma zostać
  głupie i stabilne.
- **W integracji HA**: tam jest dzisiaj, ale to drugi egzemplarz tego samego.
- **W hubie**: tam też jest, i to jest egzemplarz, który realnie działa, ma bazę
  i jest źródłem prawdy dla `tracker.example.lan`.

### Rekomendacja: discovery publikowane przez huba, w dwóch klasach tematów

Hub publikuje **same konfiguracje discovery**, retained, raz. Stan encji bierze
się z dwóch różnych miejsc, i to jest sedno propozycji:

| Klasa | `state_topic` | Co się dzieje, gdy hub padnie |
|---|---|---|
| Bezpośrednie: pozycja, napięcie, satelity, HDOP, RSSI, kolejka, tryb, dostępność | temat **urządzenia**, np. `cartracker/nd1/tel` | encje żyją dalej, bo hub nie jest w tej ścieżce |
| Pochodne: dystans przejazdu, czas, prędkość maksymalna | temat **huba**, np. `cartracker/nd1/trip` | te encje się zestarzeją, reszta nie |

Konfiguracje są retained, więc encje przeżywają restart huba, a nawet jego
całkowitą śmierć. Niezależność, o którą chodziło w README, zostaje zachowana
dokładnie tam, gdzie ma znaczenie: napięcie akumulatora i alarm ruchu idą
z urządzenia prosto do HA i nie przechodzą przez nic po drodze.

Co z tego wynika:

- Znika trzeci parser formatu i drugi egzemplarz logiki pochodnej.
- Znika 999 linii Pythona w `/config/custom_components/`, których nikt nie
  aktualizuje przy każdej zmianie HA.
- Nowy tracker pojawia się w HA sam, bez dodawania wpisu integracji.

Czego nie ma za darmo:

- Hub musi umieć publikować discovery i temat `trip`. To jest nowy kod w hubie,
  tylko w jednym miejscu zamiast w dwóch.
- Encje pochodne zależą od huba. Uznajemy to za akceptowalne, bo dystans
  przejazdu nikogo nie budzi w nocy, a napięcie akumulatora tak i ono nie zależy.
- Nazwy encji ustala discovery, nie my. Przy migracji trzeba przejrzeć
  automatyzacje, choć dziś nie ma żadnej, bo integracja nigdy nie została wdrożona.

### Rozstrzygnięte: discovery, zweryfikowane na żywo 2026-09-02

Warunek z pierwszej wersji tego punktu został spełniony. Hub publikuje
discovery, HA na VM103 utworzyło **21 encji na pojazd**, 42 na dwa auta,
i to pokrywa całą tabelę 8.2.

Test GPS end to end: naciśnięcie `Zlokalizuj teraz` w HA poszło na `cmd`,
urządzenie zrobiło fix i odesłało pozycję, a `device_tracker.nd1` pokazał
pozycję zgodną co do cyfry, dokładność 3,0 m i 7 satelitów, zgodnie co do cyfry
z tym, co zapisał hub. Dokładność liczona z HDOP tak samo jak w integracji
(HDOP × 2,5 m).

Dwie rzeczy wyszły dopiero na żywo i obie są naprawione:

1. **Po restarcie HA nie było wiadomo, gdzie stoi auto**, bo `pos` i `tel` nie
   były retained. Naprawione w firmware, opis w `05` punkt 5.1.
2. **Auto offline znikało z mapy**, bo `device_tracker` miał `availability_topic`.
   Usunięte z tej jednej encji; pozostałe je zachowały, żeby offline urządzenie
   nie pokazywało nieaktualnego napięcia.

Encje pochodne (cztery sensory przejazdu) czytają temat huba, wszystkie
pozostałe czytają tematy urządzenia. Test w repo huba pilnuje, żeby liczba
encji zależnych od huba nie urosła.

**Integracja `custom_components/car_tracker` nie jest już potrzebna.** Zostaje
w repo jako referencja do czasu, aż discovery przechodzi pełny sezon w aucie;
nie instalować jej równolegle z discovery, bo obie utworzyłyby te same encje.

## 8.4 Odporność na złe dane

Wszystko, co przychodzi z MQTT, jest traktowane jak dane, nie jak prawda:

- Payload nie będący JSON-em albo nie będący obiektem: log i porzucenie.
- Brak `lat` albo `lon`: porzucenie, bez wyjątku w logu HA.
- `hdop` gorszy niż 5,0: porzucenie. Firmware też filtruje, ale filtr po stronie
  odbiorczej przeżyje stare firmware w polu.
- Punkt z kolejki starszy niż to, co już pokazujemy, liczy się do statystyk
  przejazdu, ale nie cofa trackera na mapie.
- Zbiór widzianych `seq` jest przycinany do 2500 pozycji, żeby nie rósł w nieskończoność.

## 8.5 Usługi

| Usługa | Działanie |
|---|---|
| `car_tracker.locate` | wybudza GNSS i modem, wymusza jedną pozycję |
| `car_tracker.ping` | sprawdzenie czasu obiegu |
| `car_tracker.set_config` | publikuje retained `cfg` (klucze z docs/05 punkt 5.6) |

Komendy nigdy nie są retained. Retained `reboot` odtwarzałby się przy każdym
połączeniu i dałby pętlę restartów.

## 8.6 Zdarzenia

Każde `evt` z urządzenia trafia na szynę HA jako `car_tracker_event`
z polem `vehicle_id`. Do automatyzacji:

```yaml
automation:
  - alias: "Alarm: ruch auta na postoju"
    trigger:
      - trigger: event
        event_type: car_tracker_event
        event_data:
          event: motion_alarm
    action:
      - action: notify.whatsapp_api_notifier
        data:
          message: >-
            Ruch auta {{ trigger.event.data.vehicle_id }} bez zapłonu:
            https://maps.google.com/?q={{ trigger.event.data.lat }},{{ trigger.event.data.lon }}

  - alias: "ND1: akumulator siada"
    trigger:
      - trigger: numeric_state
        entity_id: sensor.nd1_napiecie_akumulatora
        below: 12.2
        for: "01:00:00"
    action:
      - action: notify.whatsapp_api_notifier
        data:
          message: "ND1: napięcie {{ states('sensor.nd1_napiecie_akumulatora') }} V, czas na prostownik."
```

Nazwy encji do sprawdzenia po instalacji, nie zakładać ich z góry.

## 8.7 Historia i retencja

Surowe pozycje w recorderze i w InfluxDB mają krótszą retencję niż reszta domu
(propozycja 180 dni), zagregowane przejazdy zostają. Uzasadnienie w docs/09
punkt 9.4: ślad surowy odtwarza codzienne trasy z dokładnością do minuty,
a agregat daje tę samą wartość użytkową przy mniejszej wrażliwości.

## 8.8 Testowanie bez sprzętu

```bash
export MQTT_PASS=$(pass-manager get "car-tracker MQTT (nd1)")
tools/sim_track.py --vehicle nd1 --trip --fast          # przejazd
tools/sim_track.py --vehicle nd1 --park --fast          # postój, napięcie spada
tools/sim_track.py --vehicle nd1 --alarm --fast         # ruch bez zapłonu
tools/sim_track.py --vehicle nd1 --backlog 120 --duplicate  # zaległości i duplikaty
```

Wariant `--backlog --duplicate` wysyła tę samą paczkę dwa razy. Jeżeli w HA
pojawią się podwójne punkty, deduplikacja po `seq` nie działa i to jest błąd
do naprawienia przed montażem w aucie, a nie po.
