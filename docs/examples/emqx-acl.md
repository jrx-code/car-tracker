# Szkic ACL EMQX

Dopasowany do `docs/09-bezpieczenstwo.md` punkt 9.2. To nie jest gotowa konfiguracja
produkcyjna — tylko wzorzec ról i prefiksów tematów. Hasła generuj lokalnie
(`openssl rand -base64 24`) i trzymaj w menedżerze haseł; **nigdy w repo**.

Prefiks: `cartracker/<vehicle_id>/…` (`vehicle_id`: `nd1`, `nd3`, …).

## Użytkownicy

| Użytkownik | Rola |
|---|---|
| `cartracker-nd1` | urządzenie w ND1 |
| `cartracker-nd3` | urządzenie w ND3 |
| `cartracker-ha` | Home Assistant (i hub, jeśli dzieli konto z HA) |

Osobne konto na pojazd pozwala odciąć jedno urządzenie (kradzież / wymiana)
bez ruszania reszty.

## Reguły (szkic)

### Pojazd `cartracker-<id>` (np. `cartracker-nd1`)

- **publish** wyłącznie: `cartracker/<id>/#`
  - typowo: `status`, `info`, `pos`, `tel`, `evt`, `batch`, `ack`
- **subscribe** wyłącznie:
  - `cartracker/<id>/cfg`
  - `cartracker/<id>/cmd`
- **zakaz** wszystkiego poza własnym prefiksem (w tym cudze `cmd` / `cfg`)

### Użytkownik HA / hub (`cartracker-ha`)

- **publish**:
  - `cartracker/+/cfg`
  - `cartracker/+/cmd`
  - `cartracker/+/trip` (publikuje hub, nie urządzenie — patrz `docs/05` §5.10)
  - `homeassistant/#` (MQTT discovery, jeśli hub publikuje discovery)
- **subscribe**:
  - `cartracker/#` (stan z urządzeń + trip)

### Czego unikać

- Wspólnego konta `wspolne-konto` na oba auta i HA.
- Retained na `cmd` (pętla `reboot` — `docs/05` §5.7).
- Uprawnień pojazdu do zapisu na `cfg` / `cmd` innych pojazdów.

## Przykład w notacji „allow / deny” (pseudo)

Składnia zależy od wersji EMQX (ACL file vs Dashboard vs API). Przenieś reguły
do formatu, którego używasz lokalnie; semantyka ma zostać taka sama:

```text
# cartracker-nd1
allow publish  cartracker/nd1/#
allow subscribe cartracker/nd1/cfg
allow subscribe cartracker/nd1/cmd
deny  all

# cartracker-nd3 — analogicznie z nd3

# cartracker-ha
allow publish  cartracker/+/cfg
allow publish  cartracker/+/cmd
allow publish  cartracker/+/trip
allow publish  homeassistant/#
allow subscribe cartracker/#
deny  all
```

Po utracie urządzenia: usuń użytkownika w EMQX, zrotuj hasło HA jeśli było
współdzielone, wyczyść retencję historii dla tego `vehicle_id` (`docs/09` §9.4).
