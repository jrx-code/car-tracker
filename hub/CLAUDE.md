# hub (tracker-hub)

Agregator floty trackerów: MQTT → SQLite → mapa i API. Natywny LXC (systemd +
Python), nie docker. Repo jest publiczne: adresy, numery kontenerów, nazwy kont
i wpisów w menedżerze haseł nie trafiają ani do kodu, ani do dokumentacji.
Domyślne wartości w `config.py` są neutralne, konkretne idą do env na LXC.

## Zasady

- Kod, komentarze, commity po angielsku. README po angielsku (repo publiczne).
- Zależności tylko z apt (`python3-paho-mqtt`). Bez pip, bez venv.
- Sekrety w `/etc/tracker-hub/tracker-hub.env` (640, root:tracker). W repo tylko
  `.env.example`.
- **Format danych jest wspólny z firmware i integracją HA.** Zmiana w
  `firmware/src/telemetry/packet.cpp` wymaga zmiany w `hub/tracker_hub/ingest.py`
  (`normalise_position`, `_COMPACT`) i w
  `ha-integration/custom_components/car_tracker/coordinator.py`, w tym samym
  commicie. Kontrakt: `docs/05-protokol-mqtt.md`.
- `ingest.py` jest jedynym miejscem, które interpretuje wiadomości. `store.py`
  tylko zapisuje, `web.py` tylko wyświetla. Nie rozsiewać logiki.
- Komendy MQTT nigdy nie są retained. Retained `reboot` = pętla restartów.
- paho: kod musi działać na 1.x (stacja robocza) i 2.x (Debian 13). Stąd
  `_new_client()` i callbacki z `*args`.
- Pusty `TRUSTED_PROXIES` wyłącza tryb administratora. Tak ma zostać: nagłówek
  `X-Authentik-Username` z dowolnego adresu w LAN nie może dawać admina.

## Grabie, na które już nadepnięto

- `rsync` bez `--chmod`: pliki z udziału NFS lądują jako 640/750, a usługa
  chodzi jako `tracker` i nie wchodzi nawet do katalogu pakietu. Python mówi
  wtedy "No module named tracker_hub.__main__", co nie brzmi jak problem praw.
- `systemctl enable --now` na działającej jednostce nic nie robi. Deploy musi
  wołać `restart`, inaczej wgrany kod nie wchodzi do użycia.
- paho 2.x podaje w callbacku `ReasonCode`, a nie int. `int(rc)` rzuca
  TypeError. Czytać `rc.value`. Lokalne testy na paho 1.6 tego nie złapią.
- `rsync -a` z NFS na LXC: chown kończy się EINVAL, rsync wychodzi z kodem 23
  i `set -e` przerywa deploy przed restartem. Stąd `--no-owner --no-group`.
- Świeży provider Authentika: outpost zna nowy host dopiero po odświeżeniu
  konfiguracji, do tego czasu forward_auth zwraca 404 zamiast przekierowania.

## Weryfikacja przed commitem

```bash
cd hub && python3 -m pytest tests/ -q && ruff check tracker_hub tests tools_seed.py
```
