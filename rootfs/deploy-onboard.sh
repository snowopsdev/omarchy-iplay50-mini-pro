#!/usr/bin/env bash
# rootfs/deploy-onboard.sh [extra-dir...]: push rootfs/onboard (plus any extra directories, e.g. the Wi-Fi/BT/power
# helpers from docs/05 and docs/11) to the tablet's /opt/omarchy-tablet and install the system bits
# (omarchy-hw.service, quiet-console sysctl). Does not enable services.
# Needs a running Arch on the tablet reachable with tools/tablet (TABLET_ADDRESS, TABLET_SSH_KEY, TABLET_KNOWN_HOSTS).
# (Adapted from the project's tools/deploy-onboard.sh: ssh options shared with tools/tablet; source dirs as arguments.
#  Not re-run on a device in this form.)
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
TAB=$REPO/tools/tablet
HOST=${TABLET_ADDRESS:-172.16.42.1}
SO=(-o ConnectTimeout=5 -o BatchMode=yes -o HostKeyAlias=172.16.42.1 -i "${TABLET_SSH_KEY:-$HOME/.ssh/omarchy-tablet_ed25519}"
    -o UserKnownHostsFile="${TABLET_KNOWN_HOSTS:-$HOME/.ssh/omarchy-tablet_known_hosts}")
"$TAB" 'mkdir -p /opt/omarchy-tablet'
STAGE=$("$TAB" 'mktemp -d /opt/omarchy-tablet/.deploy.XXXXXXXX')
[[ $STAGE == /opt/omarchy-tablet/.deploy.* ]] || exit 1
scp -q "${SO[@]}" "$REPO/rootfs/onboard"/* root@"$HOST":"$STAGE/"
for d in "$@"; do scp -q "${SO[@]}" "$d"/* root@"$HOST":"$STAGE/"; done
# Rename into place: in-use daemon executables cannot be overwritten by scp.
"$TAB" "mv -- '$STAGE/'* /opt/omarchy-tablet/ && rmdir -- '$STAGE'"
"$TAB" '
	cd /opt/omarchy-tablet
	install -m 644 omarchy-hw.service /etc/systemd/system/omarchy-hw.service
	install -m 644 90-omarchy-quiet.conf /etc/sysctl.d/90-omarchy-quiet.conf
	sysctl -q -p /etc/sysctl.d/90-omarchy-quiet.conf
	systemctl daemon-reload
	sha256sum hw-up.sh | cut -c1-16
	echo deployed'
