#!/usr/bin/env bash
# Create the tracker-hub LXC on a Proxmox VE host. Run ONCE, from the workstation.
#
#   CTID=<id> IP=<addr/prefix> GW=<gateway> ./provision.sh            # dry run
#   CTID=<id> IP=<addr/prefix> GW=<gateway> APPLY=1 ./provision.sh    # create it
#
# Everything after creation is deploy.sh's job.
set -euo pipefail

PVE="${PVE:-pve}"                 # ssh alias
CTID="${CTID:?set CTID}"
HOSTNAME_="${HOSTNAME_:-tracker-hub}"
IP="${IP:?set IP, e.g. 10.0.0.47/24}"
GW="${GW:?set GW}"
BRIDGE="${BRIDGE:-vmbr0}"
CORES="${CORES:-2}"
MEMORY="${MEMORY:-1024}"
DISK="${DISK:-8}"
STORAGE="${STORAGE:-local-lvm}"
TEMPLATE="${TEMPLATE:-local:vztmpl/debian-13-standard_13.1-2_amd64.tar.zst}"
APPLY="${APPLY:-0}"

run() {
  if [[ "$APPLY" == "1" ]]; then
    echo "+ $*"
    ssh "$PVE" "$@"
  else
    echo "[dry run] ssh $PVE $*"
  fi
}

echo "=== tracker-hub LXC ==="
echo "  CTID     $CTID"
echo "  hostname $HOSTNAME_"
echo "  IP       $IP  gw $GW  bridge $BRIDGE"
echo "  size     ${CORES} cores, ${MEMORY} MB RAM, ${DISK} GB on $STORAGE"
echo "  template $TEMPLATE"
[[ "$APPLY" == "1" ]] || echo "  MODE     dry run (set APPLY=1 to create)"
echo

if ssh "$PVE" "pct status $CTID" >/dev/null 2>&1; then
  echo "CTID $CTID already exists. Pick another CTID or use deploy.sh instead."
  exit 1
fi

run "pct create $CTID $TEMPLATE \
  --hostname $HOSTNAME_ \
  --cores $CORES --memory $MEMORY --swap 512 \
  --rootfs $STORAGE:$DISK \
  --net0 name=eth0,bridge=$BRIDGE,ip=$IP,gw=$GW \
  --ostype debian --unprivileged 1 --features nesting=1 \
  --onboot 1 --start 1 \
  --description 'tracker-hub: car tracker aggregator and UI (repo: car-tracker/hub)'"

if [[ "$APPLY" == "1" ]]; then
  echo "waiting for the container to come up"
  for _ in $(seq 30); do
    ssh "$PVE" "pct exec $CTID -- systemctl is-system-running --wait" >/dev/null 2>&1 && break
    sleep 2
  done
fi

run "pct exec $CTID -- bash -c 'apt-get update -qq && apt-get install -y -qq openssh-server sudo >/dev/null'"

# Push the workstation key so deploy.sh can rsync over ssh.
PUBKEY="${PUBKEY:-$HOME/.ssh/id_ed25519.pub}"
if [[ "$APPLY" == "1" && -f "$PUBKEY" ]]; then
  KEY="$(cat "$PUBKEY")"
  ssh "$PVE" "pct exec $CTID -- bash -c 'mkdir -p /root/.ssh && chmod 700 /root/.ssh && \
    echo \"$KEY\" >> /root/.ssh/authorized_keys && chmod 600 /root/.ssh/authorized_keys'"
fi

cat <<EOF

Next:
  1. DNS: point a name at the container or at the reverse proxy (see README).
  2. MQTT: create the tracker-hub user in EMQX with an ACL limited to
     cartracker/#, store the password in the password manager.
  3. TARGET=root@${IP%%/*} ./deploy.sh
  4. Reverse proxy: add the block from README.
EOF
