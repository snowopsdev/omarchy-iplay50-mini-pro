#!/usr/bin/env bash
# Install the touch keyboard binary + tablet-ui helper + bar module/config on the tablet,
# with backups, and restart only the keyboard. Run from the desktop:
#   touch-ui/deploy.sh            (needs touch-ui/out/wvkbd-mobintl from wvkbd/build.sh tablet)
# Public-repo adaptation of the original ui/deploy.sh: scp replaced by a tar stream through
# the shared helper tools/tablet (set TABLET_ADDRESS / TABLET_SSH_KEY there). The adapted
# script was not re-run against the device.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
T=$REPO/tools/tablet
# The helper's Desktop/ABC signals require the matching ARM keyboard. Never
# deploy only the helper onto an older keyboard, or copy a host-built binary.
python3 - "$REPO/touch-ui/out/wvkbd-mobintl" <<'PY'
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_bytes()
if (data[:6] != b'\x7fELF\x02\x01' or
        int.from_bytes(data[18:20], 'little') != 183 or
        b'tabletomarchy\0' not in data):
    raise SystemExit('Build the native ARM touch keyboard into touch-ui/out first: touch-ui/wvkbd/build.sh tablet')
PY
STAGE=$("$T" 'mktemp -d /opt/omarchy-tablet/.ui.XXXXXXXX')
[[ $STAGE =~ ^/opt/omarchy-tablet/\.ui\.[A-Za-z0-9]+$ ]] || exit 1
tar -cf - -C "$REPO/touch-ui/out" wvkbd-mobintl \
  -C "$REPO/touch-ui/rootfs" tablet-ui TabletButton.qml install-touch-ui.py |
  "$T" "tar -C '$STAGE' --no-same-owner -xf -"
"$T" "bash -s -- $STAGE" <<'REMOTE'
set -euo pipefail
stage=$1
install -d -m 755 /opt/omarchy-tablet/ui /opt/omarchy-tablet/backups
backup=$(mktemp -d /opt/omarchy-tablet/backups/touch-keyboard-XXXXXXXX)
chmod 700 "$backup"
for f in /usr/local/bin/wvkbd-mobintl /opt/omarchy-tablet/ui/tablet-ui; do
  [[ -e $f ]] && cp -p "$f" "$backup/"
done
chmod 600 "$backup/"* 2>/dev/null || true
pkill -TERM -u hypr -x wvkbd-mobintl || true
install -m 755 "$stage/wvkbd-mobintl" /usr/local/bin/wvkbd-mobintl.next
mv -f /usr/local/bin/wvkbd-mobintl.next /usr/local/bin/wvkbd-mobintl
install -m 755 "$stage/tablet-ui" /opt/omarchy-tablet/ui/tablet-ui.next
mv -f /opt/omarchy-tablet/ui/tablet-ui.next /opt/omarchy-tablet/ui/tablet-ui
python "$stage/install-touch-ui.py"
cd /home/hypr
runuser -u hypr -- env XDG_RUNTIME_DIR=/run/user/1001 WAYLAND_DISPLAY=wayland-1 \
  /opt/omarchy-tablet/gpu/app-env /opt/omarchy-tablet/ui/tablet-ui keyboard-start
sha256sum /usr/local/bin/wvkbd-mobintl
echo "Keyboard/helper backup: $backup"
rm -f "$stage/wvkbd-mobintl" "$stage/tablet-ui" "$stage/TabletButton.qml" "$stage/install-touch-ui.py"
rmdir "$stage"
REMOTE
