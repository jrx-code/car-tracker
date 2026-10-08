#!/usr/bin/env bash
# Build, sign and send a firmware image to one tracker over MQTT, through the hub.
#
#   OTA_KEY=/path/to/ota_key.pem HUB_URL=http://<hub>:8080 HUB_TOKEN=<token> \
#     ./scripts/ota_release.sh lilygo_a7670 nd1
#
# The hub stages the image and sends the "ota" command. The tracker checks the
# signature against the key compiled into it, fetches the image in chunks over
# its MQTT session, checks the hash and reboots into it; an image that does not
# reach the broker is rolled back (docs/07, 7.7). Progress arrives on
# <prefix>/<id>/ack, the running build is "fw" in <prefix>/<id>/info.
set -euo pipefail

ENV_NAME="${1:?usage: ota_release.sh <pio env> <vehicle id>}"
VEHICLE="${2:?usage: ota_release.sh <pio env> <vehicle id>}"
: "${OTA_KEY:?set OTA_KEY to the private signing key (PEM)}"
: "${HUB_URL:?set HUB_URL, e.g. http://<hub>:8080}"
: "${HUB_TOKEN:?set HUB_TOKEN to the hub API_TOKEN}"
PIO="${PIO:-pio}"

cd "$(dirname "${BASH_SOURCE[0]}")/.."

# The build id in "fw" is the commit hash; uncommitted code would make it lie.
if [[ -n "$(git status --porcelain -- src include platformio.ini scripts)" ]]; then
  echo "firmware/ has uncommitted changes; commit first so fw names this build" >&2
  exit 1
fi

# A key that does not match the one in the firmware gets every image refused
# by the device after the hub has already staged it. Catch that here.
want="$(sed -n 's/^ *"\(.*\)\\n";\{0,1\}$/\1/p' src/ota/ota_pubkey.h)"
have="$(openssl ec -in "$OTA_KEY" -pubout 2>/dev/null)"
if [[ "$want" != "$have" ]]; then
  echo "OTA_KEY does not match src/ota/ota_pubkey.h" >&2
  exit 1
fi

"$PIO" run -e "$ENV_NAME"
BIN=".pio/build/$ENV_NAME/firmware.bin"
SIG="$(openssl dgst -sha256 -sign "$OTA_KEY" "$BIN" | base64 -w0)"
echo "image $(stat -c %s "$BIN") bytes, sha256 $(sha256sum "$BIN" | cut -c1-16)..., build $(git rev-parse --short HEAD)"

curl -fsS -X POST \
  -H "X-Api-Token: $HUB_TOKEN" \
  -H "X-Firmware-Signature: $SIG" \
  -H "Content-Type: application/octet-stream" \
  --data-binary "@$BIN" \
  "$HUB_URL/api/vehicles/$VEHICLE/ota"
echo
