"""Entry point: wire the store, the MQTT ingest and the HTTP server together."""

from __future__ import annotations

import logging
import signal
import sys
import threading
import time

from .config import Config
from .ingest import Ingest
from .store import Store
from .web import serve

_LOG = logging.getLogger("tracker_hub")


def _housekeeping(store: Store, cfg: Config, stop: threading.Event) -> None:
    """Daily retention pass. Raw positions expire, trips stay."""
    while not stop.wait(3600):
        try:
            store.purge_old_positions(cfg.retain_positions_days)
        except Exception:  # noqa: BLE001 - housekeeping must never kill the service
            _LOG.exception("housekeeping failed")


def main() -> int:
    logging.basicConfig(
        level=logging.INFO,
        format="%(asctime)s %(levelname)-7s %(name)s: %(message)s",
    )
    cfg = Config()
    if not cfg.mqtt_pass:
        _LOG.error("MQTT_PASS is empty. Fill /etc/tracker-hub/tracker-hub.env "
                   "from the password manager and restart.")
        return 2

    _LOG.info("starting with %s", cfg.masked())
    store = Store(cfg.db_path)
    ingest = Ingest(cfg, store)
    ingest.start()

    httpd = serve(cfg, store, ingest)
    http_thread = threading.Thread(target=httpd.serve_forever, daemon=True)
    http_thread.start()

    stop = threading.Event()
    threading.Thread(
        target=_housekeeping, args=(store, cfg, stop), daemon=True
    ).start()

    def shutdown(signum, _frame):
        _LOG.info("signal %s, shutting down", signum)
        stop.set()

    signal.signal(signal.SIGTERM, shutdown)
    signal.signal(signal.SIGINT, shutdown)

    try:
        while not stop.is_set():
            time.sleep(1)
    finally:
        httpd.shutdown()
        ingest.stop()
        store.close()
        _LOG.info("stopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
