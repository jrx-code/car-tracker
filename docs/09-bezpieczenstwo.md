# 09. Bezpieczeństwo i prywatność

Tracker zna dokładne położenie dwóch aut w każdej chwili i wysyła je przez publiczną
sieć komórkową do brokera wystawionego w internecie. To jest system o realnych
konsekwencjach przy wycieku, a nie czujnik temperatury.

## 9.1 Model zagrożeń

| Zagrożenie | Skutek | Środek zaradczy |
|---|---|---|
| Kradzież urządzenia z auta razem z SIM | Obcy ma dane logowania do brokera | Poświadczenia per pojazd, natychmiastowa blokada w EMQX, ACL ograniczone do własnego prefiksu tematów |
| Podsłuch transmisji | Ujawnienie trasy i hasła MQTT | TLS do brokera; tekst jawny i TLS bez weryfikacji certyfikatu tylko do brokera w sieci lokalnej (9.3) |
| Podszycie się pod tracker | Fałszywe pozycje w HA | Uwierzytelnienie klienta, unikalny `client_id`, ACL na zapis tylko do własnego prefiksu |
| Przejęcie brokera | Historia tras obu aut | Broker jest lokalny, nie w cudzej chmurze. Retencja historii ograniczona, patrz 9.4 |
| Ktoś podszywa się pod HA i wysyła `cmd` | Zdalny restart, wyciek pozycji przez `locate` | ACL: zapis na `cmd` i `cfg` tylko dla użytkownika HA. `ota` wyłączone poza WiFi |
| Zgubiony telefon z aplikacją HA | Dostęp do historii tras | To jest problem HA, nie trackera. MFA na koncie HA |
| Karta SIM użyta do rozmów przez złodzieja | Koszty | SIM tylko z pakietem danych, blokada głosu i SMS premium u operatora |

## 9.2 Poświadczenia

- Osobny użytkownik EMQX na pojazd: `cartracker-nd1`, `cartracker-nd3`.
- Hasła generowane `openssl rand -base64 24`, przechowywane w menedżerze haseł,
  nigdy w repo.
- ACL w EMQX:
  - `cartracker-<id>` publikuje wyłącznie na `cartracker/<id>/#`,
    subskrybuje wyłącznie `cartracker/<id>/cfg` i `cartracker/<id>/cmd`.
  - użytkownik HA publikuje na `cartracker/+/cfg` i `cartracker/+/cmd`, subskrybuje resztę.
- PIN karty SIM: albo wyłączony na karcie, albo zapisany w NVS, nigdy w kodzie.
  Wyłączony PIN jest tu bezpieczniejszy niż PIN w firmware, bo firmware da się odczytać
  z flasha, a zablokowana karta po trzech restartach to wyjazd do auta.
- Flash encryption i secure boot ESP32: **`[DO DECYZJI przed produkcją]`** Włączenie
  utrudnia odczyt poświadczeń z wyjętego urządzenia, ale zamyka drogę do prostego
  wgrania firmware przez USB i wymaga trzymania kluczy. Dla dwóch urządzeń domowych
  koszt operacyjny jest wysoki, a alternatywą jest szybka rotacja hasła po utracie sprzętu.

## 9.3 TLS

Broker ma certyfikat z wildcard `*.example.lan` wystawiany przez cert-engine.
Firmware weryfikuje certyfikat po CA, nie po odcisku, żeby odnowienie certyfikatu
nie unieruchamiało trackera w trasie. Certyfikat CA jest wkompilowany w firmware
i podlega aktualizacji przez OTA.

**Uwaga z doświadczenia domowego:** broker musi podawać łańcuch RSA. Klienci
osadzeni potrafią nie obsłużyć certyfikatu ECDSA i wtedy TLS nie wstaje bez czytelnego
błędu. To trzeba potwierdzić na bench przed montażem w aucie.

**Incydent 2026-10-08: hasło MQTT otwartym tekstem.** Przy pierwszym połączeniu
po LTE broker słuchał na porcie spoza wbudowanej listy `ESP_SSLClient`, a ta
biblioteka dla takiego portu po cichu pomija TLS. Kilka ramek CONNECT z hasłem
przeszło przez internet jawnie, zanim zrzut ruchu to pokazał. Przekierowanie
zostało wyłączone, hasło konta zmienione, firmware poprawiony (07, 7.1). Wnioski,
które teraz są regułą:

- TLS jest uruchamiany jawnie i nie zależy od numeru portu; nieudane uzgadnianie
  zamyka połączenie przed wysłaniem czegokolwiek.
- Tekst jawny albo TLS bez weryfikacji certyfikatu są dozwolone tylko do brokera,
  który nie może być w internecie: prywatny adres IPv4 albo nazwa `.local`,
  `.lan`, `.home.arpa` (`util/hostcheck.h`). Portal pozwala te opcje ustawić,
  połączenie je odrzuca. CI uruchamia test tej reguły na hoście
  (`firmware/test/host`).
- Każda zmiana ścieżki połączenia jest sprawdzana zrzutem ruchu: pierwszy bajt
  od urządzenia ma być rekordem TLS (`0x16`), nie ramką MQTT (`0x10`).

**Broker widoczny z internetu.** Trackery na LTE łączą się z brokerem domowym przez
osobny listener TLS na niestandardowym porcie, przekierowany na routerze. Ten sam
broker obsługuje resztę domu, więc granica jest w ACL: klient z publicznego adresu
IPv4 może używać tylko kont `cartracker-*` i tylko gałęzi `cartracker/#`. Działa to
dlatego, że przekierowanie zachowuje adres źródłowy (sprawdzone na sesjach z LTE).
Przegląd alternatyw (Tailscale na urządzeniu, WireGuard przez PPP, Cloudflare
Tunnel z MQTT po WebSocket) zakończył się tym wyborem: zero zmian w kliencie,
TLS od urządzenia do brokera bez pośrednika, najkrótsze połączenie przy każdym
wybudzeniu.

## 9.4 Prywatność i retencja

- Historia pozycji trafia do HA i do InfluxDB. Bucket dla trackera ma **własną,
  krótszą retencję** niż reszta domu: propozycja 180 dni dla surowych punktów
  i bezterminowo dla zagregowanych przejazdów (start, koniec, dystans, czas).
- Ślad surowy pozwala odtworzyć trasy dom-praca-znajomi z dokładnością do minuty.
  Agregat do przejazdu daje tę samą wartość użytkową przy dużo mniejszej wrażliwości.
- Dashboard z mapą nie idzie na publiczny Artifact ani na wiki dostępne z zewnątrz.
- Przy sprzedaży auta: wyjąć tracker, skasować użytkownika w EMQX, wyczyścić bucket
  Influx z tego `vehicle_id`. Nie zostawiać urządzenia w aucie nowemu właścicielowi,
  bo to zmienia się w podsłuch cudzego pojazdu, i to jest już problem prawny, nie techniczny.
- Jeżeli autem jeździ ktoś inny niż właściciel, ta osoba powinna wiedzieć,
  że w aucie jest tracker.

## 9.5 Bezpieczeństwo fizyczne i pożarowe

- Bezpiecznik 500 mA przy pinie 16 to element ochrony **auta**, nie urządzenia.
- Bez ogniwa LiPo w aucie. Jeśli backup zasilania, to LiFePO4, patrz 03.5.
- Obudowa nie może blokować gniazda OBD tak, żeby serwis nie mógł się podpiąć,
  ani wystawać pod nogi kierowcy.
- Urządzenie nie może w żadnym trybie ingerować w działanie auta. Dotyczy to także
  fazy 2: nasłuch pasywny, bez ramek wysyłanych na magistralę przy jeździe.

## 9.6 Uwierzytelnianie strony floty (`tracker-hub`)

Strona floty (mapa, historia, podgląd pojazdów) jest dziś **otwarta celowo** — to
wybór prototypu, nie przeoczenie (README, next step 7). Encje w Home Assistant
chroni logowanie HA; strona floty to osobna powierzchnia ataku i **nie wolno**
traktować jej jak wewnętrznego dashboardu bez haseł, gdy w sieci pojawią się
prawdziwe auta.

**Wymaganie przed produkcją / realnymi samochodami:** strona floty musi wymagać
uwierzytelnienia. Mapa i historia przejazdów nie mogą być publiczne.

**Rekomendowane podejścia** (wybrać jedno w hubie):

1. **Reverse proxy** (Caddy / nginx / Traefik) z Basic Auth albo OIDC (np. Authelia,
   Pocket ID, Keycloak) przed `tracker-hub`.
2. **Sesja natywna w hubie** (login + cookie / token) — jeśli hub ma mieć własne
   konta niezależnie od proxy.

**Stan:** implementacja jest w `hub/`. Tryb administratora (zapisy, ustawienia
urządzeń, komendy) wymaga logowania przez Authentik na reverse proxy (podejście 1);
hub ufa nagłówkowi z tożsamością tylko z adresów `TRUSTED_PROXIES`. Otwarty
zostaje podgląd floty tylko do odczytu. Gdy i on trafi za logowanie, skreślić
punkt 7 z README.

Szkic ACL MQTT (osobna warstwa niż HTTP floty): [`docs/examples/emqx-acl.md`](examples/emqx-acl.md).
