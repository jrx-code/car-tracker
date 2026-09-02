# 06. Odczyt danych z auta (faza 2, nie v1)

W v1 tracker jest wyłącznie odbiornikiem prądu z gniazda OBD. Ten rozdział opisuje,
co dokłada faza 2, jakie są warunki jej włączenia i dlaczego nie robimy tego od razu.

## 6.1 Co byśmy zyskali

Poziom paliwa, obroty, temperatura płynu, prędkość z auta zamiast z GNSS, przebieg
całkowity, kody usterek DTC i status kontrolki check engine. W HA to sensory
uzupełniające przejazd o zużycie i o stan techniczny.

## 6.2 Dlaczego nie w v1

1. **Odpytywanie budzi auto.** Zapytanie OBD wysłane przy zamkniętym aucie wybudza
   moduły, które właśnie zasnęły. Przy pętli odpytywania co minutę auto nigdy nie
   zasypia i rozładowuje akumulator szybciej, niż nasz tracker jest w stanie
   zaoszczędzić. To sprzeczne z Z1 i Z2.
2. **Zapis na magistralę to zapis na magistralę.** Zapytanie OBD jest ramką wysłaną
   do auta. To już nie jest urządzenie pasywne. W aucie na gwarancji (ND3 2025) jest
   to argument, którego nie chcemy dawać serwisowi.
3. **Topologia magistrali w Mazdach nie jest oczywista.** W udokumentowanych przypadkach
   Mazd piny 6 i 14 gniazda prowadzą do magistrali średniej prędkości 125 kb/s,
   a magistrala wysokiej prędkości 500 kb/s wychodzi na pinach 3 i 11. To odwrotnie
   niż wynikałoby z domyślnego czytania J1962. `[DO ZMIERZENIA na ND1 i ND3 osobno:
   oscyloskopem albo analizatorem, która para pinów ma jaki ruch i z jaką prędkością.
   Nie zakładać na podstawie innych modeli Mazdy ani na podstawie standardu]`

## 6.3 Warunki wejścia w fazę 2

- v1 działa w obu autach co najmniej miesiąc i pomiar drainu potwierdza założenia z 04.
- Zmierzona topologia magistrali dla obu aut, udokumentowana w tym pliku.
- Tryb pracy: **odczyt tylko przy pracującym silniku.** Przy `st != driving`
  transceiver CAN jest odcięty load switchem i nie ma go elektrycznie na magistrali.
- Osobny load switch, żeby układ w stanie wysokiej impedancji nie obciążał terminacji.

## 6.4 Sprzęt fazy 2

| Element | Wybór | Uwaga |
|---|---|---|
| Transceiver | SN65HVD230 (3,3 V) albo TJA1051T/3 | ESP32 ma kontroler CAN na pokładzie (TWAI), więc potrzebny sam transceiver |
| Terminacja | **bez rezystora 120 Ω** | magistrala auta jest już terminowana, dokładanie trzeciego rezystora ją psuje |
| Izolacja | opcjonalnie ISO1050 | droższe, ale odcina nasze błędy od magistrali auta |

Płytka v1 ma przewidziane miejsce i wyprowadzone GPIO TWAI, żeby faza 2 nie wymagała
przeprojektowania. Piny 3 i 11 wtyku OBD są w v1 wyprowadzone na goldpiny i niepodłączone.

## 6.5 Plan implementacji fazy 2

1. Tryb pasywny: `listen only` w TWAI, zero ramek wysłanych, sam nasłuch. Zbiór dumpów
   z jazdy do pliku, analiza offline. Na tym etapie urządzenie jest nadal pasywne.
2. Identyfikacja ramek: poziom paliwa, obroty, prędkość, temperatura. Dla ND istnieją
   opisy społecznościowe, ale każdy rocznik trzeba potwierdzić samodzielnie.
3. Dopiero gdy pasywny nasłuch daje potrzebne dane, rezygnujemy z odpytywania OBD PID
   w ogóle. Ramki i tak są rozgłaszane na magistrali cyklicznie, więc pasywny odczyt
   daje to samo bez wysyłania czegokolwiek. To jest docelowy kształt fazy 2.
4. Odpytywanie PID zostaje tylko dla DTC, na żądanie z HA, przy pracującym silniku.

## Źródła

- [Madox.NET: reverse engineering magistrali CAN w Mazdach](http://www.madox.net/blog/2008/11/17/reverse-engineering-the-mazda-can-bus-part-1/)
- [mx5things: sniffer CAN w MX-5 ND](https://mx5things.blog/2017/02/18/can-bus-sniffer/)
- [Mazda6 Club: prędkości magistral HS/MS i przypisanie pinów](https://www.mazda6club.com/threads/deciphering-the-can-bus.449116/)
- [Alison Chaiken: eksperymenty z CAN w Mazdzie 3](https://she-devel.com/Mazda3_Controller_Area_Network_Experimentation.html)
