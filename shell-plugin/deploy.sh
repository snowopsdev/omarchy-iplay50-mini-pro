#!/usr/bin/env bash
# Public-repo adaptation: uses the shared SSH helper tools/tablet; not re-run against the device.
# Install the Option A touch shell plugin for the tablet's desktop user.
# Usage: shell-plugin/deploy.sh [--drop-bar-buttons]
# Backs up shell.json and any previous plugin copy under
# /opt/omarchy-tablet/backups/touch-shell-*; touches no packaged Omarchy file.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
DROP=${1:-}
T="$REPO/tools/tablet"
tar -C "$REPO/shell-plugin/plugin" -cf - tablet.touch | "$T" 'stage=$(mktemp -d /tmp/touch-shell.XXXXXX); tar -C "$stage" -xf - && echo "$stage" > /tmp/touch-shell.stage'
"$T" "bash -s -- '$DROP'" <<'REMOTE'
set -euo pipefail
drop=$1
stage=$(cat /tmp/touch-shell.stage)
home=/home/hypr
plugins=$home/.config/omarchy/plugins
backup=$(mktemp -d /opt/omarchy-tablet/backups/touch-shell-XXXXXXXX)
chmod 700 "$backup"
cp -p "$home/.config/omarchy/shell.json" "$backup/"
[[ -d $plugins/tablet.touch ]] && cp -a "$plugins/tablet.touch" "$backup/"
install -d -o hypr -g hypr "$plugins" "$home/.cache"
# Stage beside, not inside, the watched plugin directory, then swap in place.
next=$(mktemp -d "$home/.cache/tablet.touch.XXXXXX")
cp -r "$stage/tablet.touch/." "$next/"
chown -R hypr:hypr "$next"; chmod 755 "$next"
rm -rf "$plugins/tablet.touch"
mv "$next" "$plugins/tablet.touch"
rm -rf "$stage" /tmp/touch-shell.stage
python3 - "$home/.config/omarchy/shell.json" "$drop" <<'PY'
import json, os, sys
path, drop = sys.argv[1], sys.argv[2]
state = json.load(open(path))
plugins = [p for p in state.get('plugins', []) if p.get('id') != 'tablet.touch']
state['plugins'] = plugins + [{'id': 'tablet.touch'}]
if drop == '--drop-bar-buttons':
    layout = state.setdefault('bar', {}).setdefault('layout', {})
    for side in ('left', 'center', 'right'):
        layout[side] = [e for e in layout.get(side, []) if not str(e.get('id', '')).startswith('tablet.')]
tmp = path + '.touch-next'
with open(tmp, 'w') as f:
    json.dump(state, f, indent=2); f.write('\n')
st = os.stat(path)
os.chown(tmp, st.st_uid, st.st_gid); os.chmod(tmp, 0o644)
os.replace(tmp, path)
PY
export XDG_RUNTIME_DIR=/run/user/1001
runuser -u hypr -- env XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR WAYLAND_DISPLAY=wayland-1 LANG=C.UTF-8 \
  quickshell ipc -p /opt/omarchy-tablet/omarchy-runtime/shell call shell rescanPlugins || true
runuser -u hypr -- env XDG_RUNTIME_DIR=$XDG_RUNTIME_DIR WAYLAND_DISPLAY=wayland-1 LANG=C.UTF-8 \
  quickshell ipc -p /opt/omarchy-tablet/omarchy-runtime/shell call shell reloadConfig || true
echo "backup: $backup"
REMOTE
