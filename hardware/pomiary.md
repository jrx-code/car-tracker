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
| Pozycja | zgodna z rzeczywistą, wartość nie publikowana |
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

## 2026-09-07: terminator na płytce Waveshare SN65HVD230 (bench)

Warunki: płytka odłączona od wszystkiego, bez zasilania, multimetr na zakresie
rezystancji (nie na brzęczyku ciągłości), sondy na dwupinowym złączu CAN.

Odczyt: **118 Ω** między CANH a CANL.

Wniosek: **R2 jest obecny** i układ połączeń płytki zgadza się ze schematem
producenta (`03-hardware-warianty.md` 3.8), więc nie jest to klon o innej
konstrukcji. Odchyłka od nominału 120 Ω jest bez znaczenia: rezystancja
przewodów pomiarowych dodaje się do wyniku, czyli sam element ma nie więcej niż
118 Ω, co mieści się w tolerancji 5 procent.

Gdyby R2 został na miejscu, magistrala ND widziałaby 61 ∥ 118 = **40,2 Ω**
zamiast projektowanych 60 Ω, i dotyczyłoby to każdego nadajnika na HS-CAN,
łącznie z DSC, EPS i SAS. Rezystor idzie do wylutowania przed pierwszym
podpięciem do auta.

Kontrola po wylutowaniu: na tym samym złączu ma wyjść kilkadziesiąt kΩ, czyli
samo wejście różnicowe transceivera (`RDiff` 40/70/100 kΩ wg karty katalogowej,
wartość specyfikowana dla układu zasilanego). `OL` oznaczałoby urwane pole
lutownicze albo ścieżkę.

Uwaga metodologiczna: pierwszy pomiar wyszedł „brak zwarcia", bo był robiony
brzęczykiem ciągłości. Brzęczyk nie odzywa się przy 120 Ω, jego próg leży
w okolicach kilkudziesięciu omów. Terminator wykrywa się zakresem Ω i odczytem
liczby, nigdy dźwiękiem.

**Po wylutowaniu, ten sam punkt pomiarowy: 70 kΩ.** To wartość typowa `RDiff`
z karty katalogowej (40 / **70** / 100 kΩ), więc jednym odczytem potwierdzone są
trzy rzeczy: R2 zszedł, transceiver przeżył lutowanie, a złącze CAN jest
połączone z nóżkami 6 i 7 układu, bo pomiar szedł przez to złącze. Karta
specyfikuje `RDiff` dla układu zasilanego; na niezasilanej płytce wyszła ta sama
wartość typowa.

**Płytka jest gotowa do podpięcia do auta.**

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
