# Pomiary

Tylko liczby zmierzone, z datą i warunkami. Nic z katalogu, nic z pamięci.

## 2026-09-02: pierwszy fix na biurku (bench)

Warunki: ESP32 na biurku w mieszkaniu, moduł NEO-6M z anteną ceramiczną,
zasilanie 3V3 z płytki, transport WiFi (`IoT-SSID`).

| Wielkość | Wartość |
|---|---|
| Fix | **jest**, 3D |
| Satelity | 6 (maksymalnie widziane 8) |
| HDOP | **1,6** |
| Czas do pierwszego fixa (TTFF) | **151 s** |
| Prędkość transmisji | 9600 baud (domyślna u-blox) |
| Ramki NMEA poprawne / błędne | 696 / 1 |
| Bajtów odebranych | 39 761 |
| Pozycja | 52.00000, 21.00000 |
| Sygnał WiFi ESP32 | -77 dBm |

Wnioski na teraz:

- **Suma kontrolna ramek: 1 błędna na 697.** To 0,14 procent, czyli połączenie
  UART jest czyste. Gdyby masa albo prędkość były wątpliwe, ten wskaźnik
  poszedłby w dziesiątki procent.
- **HDOP 1,6 przy 6 satelitach w budynku** jest lepsze, niż zakładałem dla
  odbiornika jednosystemowego. Ocena, czy NEO-6M wystarcza, zapadnie dopiero
  po pomiarach w mieście i w ruchu (`docs/00-poc.md` punkt 0.6), ale start jest
  dobry.
- **TTFF 151 s** to zimny start bez almanachu, w budynku. Katalogowe 27 s
  dotyczy otwartego nieba. Do porównania po wyniesieniu na zewnątrz.

Uwaga metodologiczna: przez kilka godzin ten sam układ raportował zero bajtów.
Przyczyną nie był sprzęt, tylko `pinMode()` na pinie RX w firmware, który
odbierał pin UART-owi (`docs/12-bring-up.md` punkt 12.8). Wszystkie pomiary
sprzed poprawki są nieważne.

## Do zmierzenia

| Co | Gdzie opisane | Status |
|---|---|---|
| Fix pod otwartym niebem, TTFF i HDOP | `docs/00-poc.md` 0.2 | otwarte |
| Fix w ruchu, ślad przejazdu | `docs/00-poc.md` 0.6 | otwarte |
| Fix w mieście i pod drzewami | `docs/03` 3.1 (decyzja o odbiorniku) | otwarte |
| Pobór spoczynkowy aut przed montażem | `docs/11` W1 | otwarte |
| Napięcie na pinie 16 OBD w trzech stanach | `docs/11` W2 | otwarte |
| Czy pin 16 gaśnie po zaśnięciu auta | `docs/11` W3 | otwarte |
| Kalibracja dwupunktowa ADC | `docs/11` W4 | otwarte |
