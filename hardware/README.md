# Hardware

Tu lądują schematy, zdjęcia montażu i pliki obudowy. Na dziś katalog jest
szkieletem: nic nie jest kupione ani zbudowane, więc nie ma tu nic, co udawałoby
zweryfikowany projekt.

## Co ma tu być przed budową

| Plik | Zawartość | Kiedy |
|---|---|---|
| `schematy/power-tree.md` | tor zasilania z docs/04 punkt 4.3, z konkretnymi oznaczeniami elementów | po decyzji o wariancie z docs/03 |
| `schematy/pinout.md` | mapa połączeń ESP32, modem, NEO-6M, LIS3DH, zgodna z `firmware/include/pins.h` | j.w. |
| `obudowa/` | model obudowy pod wtyk OBD, druk na Prusa | po zmierzeniu miejsca pod deską w obu autach |
| `pomiary.md` | wyniki kroków W1-W5 i Z3-Z6 z docs/11, osobno dla ND1 i ND3 | w trakcie |

## Zasada

Pliku ze schematem nie tworzymy z pamięci ani z ogólnych wzorów z internetu.
Powstaje po pomiarach na konkretnym aucie i po wyborze konkretnych układów,
z numerami katalogowymi. Do tego czasu dokumentem odniesienia jest
`docs/04-zasilanie-obd.md`.
