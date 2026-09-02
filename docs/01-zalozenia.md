# 01. Założenia projektu

## 1.1 Cel

Dwa niezależne urządzenia (po jednym na auto), które:

1. Znają pozycję auta i podają ją do Home Assistant jako `device_tracker`.
2. Wykrywają jazdę i postój bez ingerencji w instalację auta.
3. Rejestrują przejazdy (start, koniec, dystans, czas, prędkość maksymalna i średnia).
4. Alarmują o ruchu auta bez kierowcy (auto zaparkowane, a zmienia pozycję).
5. Pilnują akumulatora auta i raportują napięcie, w szczególności dla ND1, który stoi zimą.
6. Nie rozładowują akumulatora, gdy auto stoi tygodniami.

## 1.2 Pojazdy

| | ND1 | ND3 |
|---|---|---|
| Rocznik | 2016 | 2025 |
| Generacja MX-5 | ND pierwsza seria | ND po drugim faceliftingu |
| Rola | auto sezonowe, długie postoje | auto bieżące |
| Priorytet | ochrona akumulatora, alarm ruchu | historia przejazdów, pozycja |
| Gniazdo OBD-II | SAE J1962, pod kolumną kierownicy `[DO ZMIERZENIA: dokładna lokalizacja i czy pin 16 jest stale zasilany]` | jw. `[DO ZMIERZENIA]` |

Różnice istotne dla projektu opisuje `10-pojazdy.md`.

## 1.3 Zakres (co robimy)

- Odbiór pozycji z GNSS (NEO-6M w v1, GNSS modemu jako druga ścieżka w v2).
- Transmisja przez LTE do brokera EMQX (`mqtt.example.lan:8883`, TLS).
- Zasilanie z pinu 16 gniazda OBD-II, z własnym zabezpieczeniem i odcięciem
  podnapięciowym.
- Detekcja stanu jazda/postój z dwóch niezależnych źródeł (napięcie instalacji
  oraz akcelerometr), nie z jednego.
- Bufor offline: brak zasięgu nie gubi danych, punkty lądują w kolejce we flashu
  i są dosyłane po odzyskaniu łącza.
- OTA firmware przez WiFi w garażu (LTE tylko awaryjnie, ze względu na transfer).
- Integracja HA z encjami, przejazdami i geofence.

## 1.4 Poza zakresem v1 (świadome decyzje)

- **Zapis na magistralę CAN.** Urządzenie nigdy nie wysyła ramek do auta. Odczyt CAN
  jest rozważony jako faza 2, opisany w `06-can-obd.md`, ale w v1 nie ma
  transceivera podłączonego do pinów 6/14.
- **Immobilizer, zdalne unieruchamianie.** Nie robimy. Ingerencja w układy
  bezpieczeństwa auta i ryzyko prawne przy sprzedaży pojazdu.
- **Podsłuch kabiny, kamera.** Nie robimy.
- **Własny serwer w chmurze.** Broker jest u nas (EMQX), HA jest u nas. Brak
  zależności od cudzej usługi trackingowej.
- **Backup na własnym akumulatorze w v1.** Rozważony w `03-hardware-warianty.md`
  jako element wariantu D. Zwiększa ryzyko pożarowe w aucie stojącym na słońcu,
  więc jeżeli wchodzi, to tylko LiFePO4, nie LiPo.

## 1.5 Założenia twarde

| Nr | Założenie | Konsekwencja projektowa |
|---|---|---|
| Z1 | Auto może stać 8 tygodni bez uruchomienia | Pobór w postoju musi być poniżej 2 mA średnio, patrz 1.7 |
| Z2 | Akumulator auta jest wspólnym zasobem, nie zasilaczem | Twarde odcięcie przy napięciu poniżej progu, tracker milknie zanim auto nie odpali |
| Z3 | Urządzenie musi dać się wypiąć w 5 sekund | Wtyk OBD, żadnego lutowania w instalacji auta, żadnych złączy pod deską |
| Z4 | Brak zasięgu jest stanem normalnym, nie awarią | Kolejka offline z zapisem do flash, wysyłka wsteczna z zachowanymi znacznikami czasu |
| Z5 | Auto z trackerem jedzie za granicę | Modem i taryfa muszą działać w roamingu UE, patrz 1.6 |
| Z6 | Firmware musi być jeden dla obu aut | Konfiguracja per pojazd z NVS, nie z `#define` przy kompilacji |
| Z7 | Sekrety nie trafiają do repo ani do logów | `config.h` w gitignore, PIN SIM i hasło MQTT w NVS, log maskuje wartości |

## 1.6 Założenia sieciowe

- Karta SIM: M2M lub zwykła prepaid z pakietem danych, wymagana praca w roamingu UE.
- Wolumen: przy 30 s interwale w jeździe i pakiecie 120 B netto to około
  14 kB na godzinę jazdy, czyli około 2 MB miesięcznie przy 150 h jazdy.
  Z narzutem TLS i keepalive przyjmujemy budżet 20 MB na miesiąc na auto.
- **Ryzyko technologiczne:** LTE-M i NB-IoT mają w Polsce nierówne pokrycie i słaby
  roaming. To główny argument przeciw wariantowi z SIM7080G jako jedynym modemem,
  szczegóły w `03-hardware-warianty.md`. `[DO WERYFIKACJI u operatora, którego SIM kupimy: czy w taryfie jest LTE-M/NB-IoT i czy działa roaming]`
- 3G w Polsce jest wyłączony u wszystkich operatorów, więc fallback 3G nie istnieje.
  2G jeszcze działa, ale jest w planach wygaszania, więc nie może być jedyną warstwą.
  `[DO WERYFIKACJI: aktualny stan wygaszania 2G u wybranego operatora w momencie zakupu]`

## 1.7 Budżet prądowy (kryterium akceptacji)

Punkt odniesienia: akumulator MX-5 ND `[DO ZMIERZENIA: pojemność, ND ma mały akumulator]`.
Przyjmujemy roboczo 45 Ah i dopuszczalny ubytek 20 procent w 8 tygodni, czyli 9 Ah
na 1344 h, co daje **6,7 mA średnio**. To górna granica z zapasem na samorozładowanie
akumulatora i pobór własny auta, więc cel projektowy ustawiamy dziesięciokrotnie niżej.

| Tryb | Cel | Uwaga |
|---|---|---|
| Jazda (GNSS + modem online) | do 250 mA | alternator pracuje, nieistotne dla akumulatora |
| Postój, czuwanie (deep sleep, akcelerometr aktywny) | **poniżej 2 mA** | to jest kryterium akceptacji projektu |
| Postój, hibernacja (napięcie poniżej progu) | poniżej 200 µA | tracker wyłącza się sam, budzi po podniesieniu napięcia |

**Konsekwencja:** typowa płytka ESP32 DevKit nie przejdzie tego testu. Regulator
AMS1117 i układ USB-serial ciągną rzędu 10 mA nawet gdy ESP32 śpi, co daje ponad
6 Ah w 8 tygodni z samego marnotrawstwa. Dlatego przetwornica 12 V na 3,3 V musi
mieć niski prąd spoczynkowy, a moduły GNSS i LTE muszą być odcinane load switchem,
nie tylko usypiane. Szczegóły i pomiar w `04-zasilanie-obd.md`.

## 1.8 Kryteria akceptacji v1

1. Pobór w postoju zmierzony na stole przy 12,6 V wynosi poniżej 2 mA.
2. Tracker po wpięciu do OBD łączy się z EMQX i pojawia się w HA jako urządzenie
   z encjami bez ręcznej konfiguracji encji (discovery po stronie integracji).
3. Przejazd 20 km jest odtworzony w HA z dokładnością pozycji poniżej 20 m
   i bez luk dłuższych niż 2 minuty w miejscu z zasięgiem.
4. Wyjęcie anteny LTE na 10 minut w trakcie jazdy nie gubi żadnego punktu:
   po ponownym zasięgu punkty dochodzą z oryginalnymi znacznikami czasu.
5. Zjazd napięcia poniżej progu hibernacji powoduje wysłanie ostatniego pakietu
   z informacją o hibernacji i ciszę do czasu podniesienia napięcia.
6. Odpięcie trackera od OBD daje w HA stan niedostępny w czasie poniżej 3 minut
   (LWT brokera), a nie zamrożoną ostatnią pozycję.
