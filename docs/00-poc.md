# 00. PoC na sprzęcie, który już jest

**Decyzja: proof of concept budujemy wyłącznie z tego, co leży w szufladzie.**
Żadnych zakupów przed PoC. Cel jest jeden: sprawdzić, czy pomysł działa
end to end, zanim wydamy pieniądze na modem, przetwornicę i anteny.

## 0.1 Sprzęt PoC

| Element | Co mam | Rola w PoC |
|---|---|---|
| MCU | ESP32 (WiFi + BT) | całość logiki, ten sam kod co docelowo |
| GNSS | NEO-6M-0-001 | pozycja, ten sam kod co docelowo |
| Łączność | **WiFi zamiast LTE** | hotspot z telefonu w aucie albo WiFi w garażu |
| Zasilanie | USB (powerbank albo gniazdo 12 V w aucie) | **nie z OBD**, patrz 0.4 |
| Akcelerometr | brak, jeśli nie ma LIS3DH pod ręką | detekcja jazdy z samej prędkości GNSS, patrz 0.3 |

Środowisko build: `pio run -e wifi_dev`. To nie jest osobna gałąź ani osobny
projekt, tylko jedno z pięciu środowisk tego samego firmware. PoC i wersja
docelowa dzielą maszynę stanów, kolejkę offline, protokół i integrację HA.
Zmienia się wyłącznie warstwa transportu.

## 0.2 Co PoC realnie weryfikuje

To jest lista rzeczy, które można sprawdzić bez modemu, i które są
najczęstszym źródłem porażki tego typu projektów:

1. **Jakość fixa z NEO-6M w aucie.** Ile satelitów, jaki HDOP, jak długo trwa
   zimny start, jak bardzo pogarsza się to z anteną w kokpicie zamiast pod
   podszybiem. To rozstrzyga, czy w wersji docelowej potrzebny jest odbiornik
   wielosystemowy (docs/03 punkt 3.1).
2. **Kolejka offline.** Jazda poza zasięgiem hotspotu to dokładnie ten sam
   przypadek co jazda poza zasięgiem LTE. Po powrocie do garażu punkty mają
   dojść z oryginalnymi znacznikami czasu, bez luk i bez duplikatów w HA.
   To jest **najlepszy test PoC**, bo weryfikuje najbardziej ryzykowną część
   firmware bez żadnego dodatkowego sprzętu.
3. **Cały łańcuch danych.** ESP32, TLS, EMQX, integracja, mapa, przejazd,
   statystyki. Jeżeli coś jest źle pomyślane w protokole, wyjdzie tutaj.
4. **Wierność śladu.** Czy interwał 30 s plus wyzwalacz na zmianę kursu daje
   sensowną trasę na zakrętach, czy trzeba go zmieniać.
5. **Zachowanie w tunelu i pod wiaduktem.** Jak firmware radzi sobie z utratą
   fixa i czy nie wysyła śmieciowych pozycji.

## 0.3 Czego PoC nie sprawdzi i trzeba to wiedzieć

| Nie sprawdzi | Dlaczego | Kiedy to zweryfikować |
|---|---|---|
| Budżetu prądowego | Zasilanie z USB, nie z akumulatora auta. Deep sleep i load switche nie mają tu znaczenia | Krok Z3-Z6 w docs/11, na zasilaczu laboratoryjnym |
| Zasięgu i roamingu | WiFi to nie LTE | Po zakupie modemu, faza 2 w docs/11 |
| Detekcji zapłonu z napięcia | Bez zasilania z pinu 16 nie ma czego mierzyć | Krok W2 w docs/11 |
| Alarmu ruchu na postoju | Bez akcelerometru brak wake on motion | Po dołożeniu LIS3DH |
| Zachowania przy zaniku zasilania w aucie | Powerbank nie gaśnie razem z autem | Faza 3 w docs/11 |

**Jeżeli nie ma akcelerometru:** `motion::begin()` zwraca `false`, gdy nie
wykryje LIS3DH na I2C, a `sustainedMotion()` zwraca wtedy zawsze `false`.
Firmware działa dalej, tylko detekcja jazdy opiera się wyłącznie na napięciu,
którego w PoC też nie ma. Dlatego na czas PoC ustaw w konfiguracji
`v_drive_on` na wartość poniżej napięcia zasilania z USB widzianego przez ADC,
albo po prostu trzymaj urządzenie w trybie DRIVING przez cały test. Docelowa
logika dwóch źródeł (docs/02 punkt 2.4) jest testowana dopiero w aucie.

## 0.4 Zasilanie w PoC: świadomie nie z OBD

PoC nie wpina się w gniazdo OBD. Powód jest prosty: dopóki nie ma zmierzonego
poboru w deep sleep i twardego odcięcia podnapięciowego, urządzenie wpięte na
stałe do akumulatora jest ryzykiem, a nie eksperymentem. Powerbank albo gniazdo
12 V z przetwornicą USB daje te same dane pozycyjne przy zerowym ryzyku dla auta.

Kolejność jest w docs/11 i nie należy jej skracać: najpierw pomiar poboru
spoczynkowego auta (W1), potem budżet prądowy na stole (Z3-Z6), a dopiero
potem wtyk OBD.

## 0.5 Jak uruchomić PoC

```bash
cd firmware
cp src/config.example.h src/config.h
# wypełnij: WIFI_SSID/WIFI_PASS (hotspot telefonu), MQTT_USER/MQTT_PASS,
# MQTT_ROOT_CA (CA brokera). Wartości z menedzer hasel, plik jest w .gitignore.
pio run -e wifi_dev -t upload
pio device monitor
```

Po stronie HA: skopiuj integrację, dodaj wpis z `vehicle_id` = `nd1`
(patrz docs/08). Zanim wsiądziesz do auta, przepuść symulator, żeby mieć
pewność, że problem w polu jest problemem urządzenia, a nie integracji:

```bash
export MQTT_PASS=$(bw get password "car-tracker MQTT (nd1)")
tools/sim_track.py --vehicle nd1 --trip --fast
tools/sim_track.py --vehicle nd1 --backlog 120 --duplicate
```

## 0.6 Kryteria zamknięcia PoC

PoC jest zamknięty, gdy:

1. Przejazd testowy jest widoczny w HA jako ciągły ślad, a nie zbiór punktów
   z dziurami w miejscach z zasięgiem hotspotu.
2. Jazda poza zasięgiem hotspotu i powrót do garażu daje komplet punktów
   z oryginalnymi znacznikami czasu i bez duplikatów.
3. Znane są liczby z NEO-6M w aucie: typowa liczba satelitów, typowy HDOP,
   czas zimnego startu, różnica między anteną w kokpicie a pod podszybiem.
4. Wiadomo, czy interwał i próg zmiany kursu wymagają korekty.

Dopiero wtedy ma sens decyzja zakupowa z docs/03 i wydawanie pieniędzy na modem.
Jeżeli PoC pokaże, że NEO-6M gubi fix w mieście, decyzja zakupowa zmienia się
z jednej pozycji (modem) na dwie (modem plus odbiornik wielosystemowy),
i lepiej wiedzieć to przed zamówieniem, a nie po.
