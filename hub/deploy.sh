#!/usr/bin/env bash
# Deploy tracker-hub to its LXC. Idempotent: safe to re-run after every change.
#
#   TARGET=root@<lxc-address> ./deploy.sh
#
# The container itself is created by provision.sh, which runs once.
set -euo pipefail

TARGET="${TARGET:?set TARGET=root@<lxc-address>}"
APP_DIR=/opt/tracker-hub
ETC_DIR=/etc/tracker-hub
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

say() { printf '\033[1;34m==>\033[0m %s\n' "$*"; }

say "target: $TARGET"
ssh -o ConnectTimeout=10 "$TARGET" true

say "system packages"
ssh "$TARGET" bash -euo pipefail <<'REMOTE'
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
# paho from apt, not pip: no venv to maintain and it tracks Debian security fixes.
apt-get install -y -qq python3 python3-paho-mqtt ca-certificates curl rsync >/dev/null
id -u tracker >/dev/null 2>&1 || useradd --system --home /var/lib/tracker-hub \
    --shell /usr/sbin/nologin tracker
install -d -o tracker -g tracker -m 750 /var/lib/tracker-hub
install -d -m 750 /etc/tracker-hub
install -d -m 755 /opt/tracker-hub
REMOTE

say "vendored Leaflet (so the UI works without reaching a CDN)"
mkdir -p "$HERE/web/vendor"
if [[ ! -f "$HERE/web/vendor/leaflet.js" ]]; then
  curl -fsSL -o "$HERE/web/vendor/leaflet.js" \
    https://unpkg.com/leaflet@1.9.4/dist/leaflet.js
  curl -fsSL -o "$HERE/web/vendor/leaflet.css" \
    https://unpkg.com/leaflet@1.9.4/dist/leaflet.css
  curl -fsSL -o "$HERE/web/vendor/images/marker-shadow.png" --create-dirs \
    https://unpkg.com/leaflet@1.9.4/dist/images/marker-shadow.png
fi

say "code"
# --chmod is not optional: rsync copies the source mode, and files coming from
# an NFS-mounted CodeHub arrive as 640/750. The service runs as the unprivileged
# `tracker` user, which then cannot even traverse the package directory, and
# Python reports it as "No module named tracker_hub.__main__".
# --no-owner/--no-group: the uid of a file on NFS means nothing on the LXC and
# chown fails with EINVAL (rsync exit 23 stops the script); ownership is set
# explicitly in the restart step below anyway.
rsync -az --no-owner --no-group --delete --chmod=D755,F644 \
  --exclude '__pycache__' --exclude '.pytest_cache' --exclude 'tests' \
  "$HERE/tracker_hub" "$HERE/web" "$TARGET:$APP_DIR/"

say "systemd unit"
rsync -az --chmod=F644 "$HERE/systemd/tracker-hub.service" "$TARGET:/etc/systemd/system/"

if ! ssh "$TARGET" test -f "$ETC_DIR/tracker-hub.env"; then
  say "no env file on the target, installing the template"
  rsync -az "$HERE/.env.example" "$TARGET:$ETC_DIR/tracker-hub.env"
  ssh "$TARGET" "chown root:tracker $ETC_DIR/tracker-hub.env && chmod 640 $ETC_DIR/tracker-hub.env"
  echo
  echo "  Fill in MQTT_PASS on the target from the password manager, then re-run:"
  echo "    ssh $TARGET vi $ETC_DIR/tracker-hub.env"
  echo
fi

say "restart"
ssh "$TARGET" bash -euo pipefail <<'REMOTE'
chown -R root:root /opt/tracker-hub
chmod -R a+rX /opt/tracker-hub
systemctl daemon-reload
systemctl enable --quiet tracker-hub.service
# restart, not "enable --now": on an already running unit the latter is a no-op
# and the freshly deployed code would not be picked up.
systemctl restart tracker-hub.service
sleep 3
systemctl is-active --quiet tracker-hub.service || {
  echo "service failed to start:"; journalctl -u tracker-hub -n 30 --no-pager; exit 1; }
REMOTE

say "health"
ssh "$TARGET" 'curl -sf http://127.0.0.1:8080/api/health' | head -c 400
echo
say "done"
