# 13. Portal konfiguracyjny i tryb admin

Urządzenie wystawia własną stronę: status dla każdego, ustawienia po przełączeniu
w tryb admin. Powód jest praktyczny. Do tej pory wszystko siedziało w `config.h`,
więc zmiana hasła WiFi albo adresu brokera oznaczała rekompilację i kabel. Przy dwóch
autach i urządzeniu wciśniętym pod deskę rozdzielczą to zły model.

**`config.h` zostaje jako ustawienia fabryczne.** Jego wartości są używane na czystym
urządzeniu i po przywróceniu ustawień fabrycznych. Poza tym źródłem prawdy jest NVS.

## 13.1 Dwie drogi dostępu

1. **Przez zwykłą sieć WiFi** — bench, garaż, aktualizacje.
2. **Przez awaryjny AP, który urządzenie samo podnosi**, gdy jest offline dłużej niż
   ustawiony czas. To jest właściwy powód istnienia portalu: tracker pod deską
   z błędnym hasłem WiFi albo martwą kartą SIM nie ma żadnego innego wejścia.
   Nie ma konsoli, nie ma sieci, nie ma się do czego podłączyć.

AP działa w trybie AP+STA, więc urządzenie nie przestaje próbować wrócić do sieci.
Po powrocie AP jest zwijany, bo otwarte wejście na urządzeniu, które jest już
osiągalne normalną drogą, to zbędna powierzchnia ataku. Jest też limit czasu:
AP wiszący dobę w zaparkowanym aucie to pobór prądu i ryzyko.

Do AP dołożony jest DNS przechwytujący wszystkie nazwy, więc telefon po połączeniu
ląduje na stronie zamiast na błędzie przeglądarki.

## 13.2 Układ panelu

Jedna strona, żadnej nawigacji. Na górze status w kafelkach i trzy plakietki
(sieć, MQTT, GPS), niżej zwijane sekcje, na dole przyklejony pasek z hasłem
administratora i przyciskiem zapisu.

- **Sekcje zwijane.** Siedem: Pojazd, WiFi, AP awaryjny, MQTT, LTE, Piny,
  Portal i serwis. Które są otwarte, pamięta przeglądarka, więc powrót do
  poprawiania jednego pola nie zaczyna się od przewijania wszystkiego.
- **Objaśnienie przy każdym polu.** Nie tylko co to jest, ale co się stanie:
  „za krótki czas oznacza, że AP wstaje przy każdym mrugnięciu sieci",
  „trzy błędne próby PIN to wyjazd do auta".
- **Walidacja przy wpisywaniu.** Pole robi się czerwone, komunikat pojawia się
  pod nim, a nagłówek zwiniętej sekcji pokazuje licznik błędów. Próba zapisu
  z błędami rozwija sekcje, w których siedzą, zamiast zostawiać szukanie.
- **Pasek zapisu pojawia się dopiero po zmianie czegokolwiek** i mówi wprost,
  że zmiany nie są zapisane. Wyjście ze strony z niezapisanymi zmianami pyta
  o potwierdzenie.

Reguły walidacji w przeglądarce są kopią tych, które wymusza firmware. Sprawdzenie
po stronie urządzenia zostaje mimo to: portal jest tylko jednym z klientów API,
a walidacja w przeglądarce jest wygodą, nie zabezpieczeniem.

## 13.3 Tryb admin

Przycisk **Tryb admin** odsłania panel ustawień. Bez klikania widać tylko status.

Rozdział odpowiedzialności jest celowy:

- **Odczyt jest otwarty.** Status i wartości ustawień (bez haseł) widać bez logowania.
  Blokada, która nie pozwala właścicielowi odczytać stanu, gdy stoi przy martwym
  trackerze, jest gorsza niż bezużyteczna.
- **Zapis wymaga hasła administratora.** Wpisuje się je raz w panelu i idzie
  w nagłówku `X-Admin-Pass` przy każdym zapisie.
- **Hasła nigdy nie wracają do przeglądarki.** Portal podaje wyłącznie `<klucz>_set`,
  czyli informację czy hasło jest ustawione, nigdy jego treść. Puste pole hasła przy
  zapisie oznacza „nie zmieniaj", więc wysłanie formularza bez wpisywania haseł
  niczego nie kasuje.

## 13.4 Co da się ustawić

### Pojazd

| Ustawienie | Po co |
|---|---|
| `vehicle_id` | tworzy temat MQTT, musi się zgadzać z wpisem w HA i w hubie |
| `vehicle_name` | opis pokazywany tylko w portalu |
| `hostname` | nazwa mDNS i DHCP |

### WiFi

| Ustawienie | Po co |
|---|---|
| `wifi_enabled` | całkowite wyłączenie radia w wariancie czysto LTE |
| `wifi_ssid`, `wifi_pass` | sieć domowa albo hotspot z telefonu |
| `wifi_timeout_s` | ile prób łączenia przed uznaniem, że jest offline |

W panelu jest skan sieci: lista SSID z siłą sygnału podpowiadana do pola.

### AP awaryjny

| Ustawienie | Po co |
|---|---|
| `ap_enabled` | wyłączenie, jeśli ktoś nie chce tej drogi wejścia |
| `ap_ssid` | puste znaczy `cartracker-<vehicle_id>` |
| `ap_pass` | minimum 8 znaków albo puste dla sieci otwartej |
| `ap_after_s` | po ilu sekundach offline podnieść AP |
| `ap_timeout_s` | po ilu sekundach go zwinąć, 0 znaczy nigdy |

Hasło krótsze niż 8 znaków jest odrzucane, a nie skracane: WPA2 takiego klucza nie
przyjmie i AP wstałby po cichu jako sieć otwarta.

### MQTT

| Ustawienie | Po co |
|---|---|
| `mqtt_host`, `mqtt_port` | broker |
| `mqtt_user`, `mqtt_pass` | konto per pojazd |
| `mqtt_tls` | szyfrowanie |
| `mqtt_verify_ca` | weryfikacja certyfikatu brokera |
| `topic_prefix` | domyślnie `cartracker` |
| `mqtt_keepalive` | wpływa na to, jak szybko HA zobaczy `offline` z LWT |
| certyfikat CA | wklejany jako PEM, trzymany w LittleFS |

Certyfikat leży w LittleFS, nie w NVS: łańcuch PEM jest większy niż wygodnie mieści
się we wpisie NVS. Przy zapisie sprawdzane są linie `BEGIN`/`END CERTIFICATE`, bo
ucięte wklejenie to najczęstszy błąd, a objawiłby się dopiero jako niewyjaśniony
błąd uzgadniania TLS.

Włączenie weryfikacji bez wgranego certyfikatu jest **odrzucane**, a nie po cichu
ignorowane. To jest ta sama zasada co przy progach napięcia: konfiguracja z sieci
nie może wprowadzić urządzenia w stan, w którym nie da się już do niego dostać.

### LTE

| Ustawienie | Po co |
|---|---|
| `modem_enabled` | wyłączenie modemu na czas PoC |
| `apn`, `apn_user`, `apn_pass` | zależne od operatora |
| `sim_pin` | puste znaczy PIN wyłączony na karcie, tak jest bezpieczniej (docs/09) |
| `allow_roaming` | jazda za granicę |

### Piny

Wszystkie: GNSS RX/TX/zasilanie i prędkość, modem RX/TX/PWRKEY/zasilanie, wejście
pomiaru napięcia, przerwanie akcelerometru, I2C, dioda.

To jest świadoma decyzja, a nie wygoda. Cały dzień poszedł na odbiornik, który
milczał z powodu firmware (`docs/12` punkt 12.8), i możliwość przestawienia pinu
z przeglądarki jest warta więcej niż elegancja stałej kompilacyjnej.

Walidacja odrzuca GPIO 6-11 (obsługują pamięć flash, sterowanie nimi uniemożliwia
start) oraz wartości spoza 0-39. RX i TX GNSS nie mogą być tym samym pinem.

### Portal i serwis

| Ustawienie | Po co |
|---|---|
| `admin_pass` | hasło do zapisu, nie do odczytu |
| `portal_enabled` | całkowite wyłączenie serwera |
| `ota_enabled` | aktualizacja przez sieć |

Akcje: restart, ustawienia fabryczne (z potwierdzeniem), ręczne podniesienie AP.

## 13.5 Czego świadomie nie ma

- **Uwierzytelniania na odczyt.** Uzasadnienie w 13.3.
- **HTTPS na urządzeniu.** Certyfikat na ESP32 w aucie oznaczałby albo certyfikat
  samopodpisany z ostrzeżeniem w przeglądarce, albo odnawianie czegoś, do czego nie
  ma dostępu z zewnątrz. Portal jest osiągalny z LAN albo z własnego AP urządzenia.
- **Edycji progów napięcia i interwałów.** Te są w retained `cfg` przez MQTT
  (`docs/05` punkt 5.6) i mają być ustawiane centralnie z HA albo z huba, żeby oba
  auta dało się zmienić bez podchodzenia do każdego.
- **Zapisu logów na urządzeniu.** Flash jest zajęty kolejką pozycji, a kolejka jest
  ważniejsza.

## 13.6 Certyfikat brokera: pułapka, która kosztowała pół godziny

Broker `mqtt.example.lan` podaje łańcuch zakotwiczony w **ISRG Root YR**, a nie
w ISRG Root X1. Root YR jest cross-signed przez X1, więc przeglądarka i OpenSSL
budują ścieżkę bez problemu, ale **BearSSL na ESP32 tego nie zrobi**: z X1 jako
kotwicą uzgadnianie TLS pada, z Root YR przechodzi.

Objaw był mylący, bo `Verify return code: 0 (ok)` w `openssl s_client` sugeruje,
że wszystko jest w porządku. Rozstrzygnął dopiero test rozdzielający: wyłączenie
weryfikacji certyfikatu (`mqtt_verify_ca` na false) natychmiast dało połączenie,
co przeniosło winę z warstwy TLS na sam certyfikat.

Właściwy certyfikat wyciąga się z łańcucha serwera:

```bash
openssl s_client -connect mqtt.example.lan:8883 -servername mqtt.example.lan -showcerts \
  </dev/null 2>/dev/null | awk '/BEGIN CERT/{n++} n==3{print}' > root.pem
```

i wkleja w portalu. Zasada na przyszłość: dla klienta na BearSSL bierz kotwicę
**z łańcucha, który realnie wysyła serwer**, a nie ogólnie znany root tej samej
marki. Root YR wygasa 2032-09-02, więc do tego czasu temat wraca.

## 13.7 Co wymaga restartu

Zapis ustawień jest natychmiastowy w NVS, ale nie wszystko przeładowuje się w locie.
Restart jest potrzebny po zmianie: pinów, prędkości GNSS, parametrów sieci WiFi,
adresu brokera, certyfikatu i trybu TLS. Portal mówi to po zapisie, zamiast udawać,
że zmiana już działa.
