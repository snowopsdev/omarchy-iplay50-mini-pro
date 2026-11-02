#!/usr/bin/env bash
# Public-repo adaptation: uses the shared SSH helper tools/tablet.
# Install the tablet PIN lock screen (plugin tablet.lock) for the desktop user.
# Usage: shell-plugin/deploy-lock.sh [IDLE_LOCK_SECONDS]   (default 300; 0 = never)
# - /opt/omarchy-tablet/lock/{tablet-lock-pin,tablet-power-key}
# - ~/.config/omarchy/plugins/tablet.lock; shell.json enables it, disables
#   omarchy.lock (same `lock` IPC target) and sets idle.lock
# - bindings.lua: the power key runs tablet-power-key (marked block)
# Backs up shell.json and bindings.lua under /opt/omarchy-tablet/backups/lock-*.
# Restart the shell afterwards (shell-plugin/restart-shell): swapping the lock
# service in place would briefly leave two `lock` IPC handlers.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
IDLE=${1:-300}
T="$REPO/tools/tablet"
tar -C "$REPO/shell-plugin" -cf - lock plugin/tablet.lock | "$T" 'stage=$(mktemp -d /tmp/tablet-lock.XXXXXX); tar -C "$stage" -xf - && echo "$stage" > /tmp/tablet-lock.stage'
"$T" "bash -s -- '$IDLE'" <<'REMOTE'
set -euo pipefail
idle=$1
stage=$(cat /tmp/tablet-lock.stage)
home=/home/hypr
plugins=$home/.config/omarchy/plugins
bindings=$home/.config/hypr/bindings.lua
backup=$(mktemp -d /opt/omarchy-tablet/backups/lock-XXXXXXXX)
chmod 700 "$backup"
cp -p "$home/.config/omarchy/shell.json" "$bindings" "$backup/"
[[ -d $plugins/tablet.lock ]] && cp -a "$plugins/tablet.lock" "$backup/"

install -d -m 755 /opt/omarchy-tablet/lock
install -m 755 "$stage/lock/tablet-lock-pin" "$stage/lock/tablet-power-key" /opt/omarchy-tablet/lock/

install -d -o hypr -g hypr "$plugins" "$home/.cache"
next=$(mktemp -d "$home/.cache/tablet.lock.XXXXXX")
cp -r "$stage/plugin/tablet.lock/." "$next/"
chown -R hypr:hypr "$next"; chmod 755 "$next"
rm -rf "$plugins/tablet.lock"
mv "$next" "$plugins/tablet.lock"
rm -rf "$stage" /tmp/tablet-lock.stage

python3 - "$home/.config/omarchy/shell.json" "$idle" <<'PY'
import json, os, sys
path, idle = sys.argv[1], int(sys.argv[2])
state = json.load(open(path))
state['plugins'] = [p for p in state.get('plugins', []) if p.get('id') != 'tablet.lock'] + [{'id': 'tablet.lock'}]
disabled = [d for d in state.get('disabledPlugins', []) if d != 'omarchy.lock']
state['disabledPlugins'] = disabled + ['omarchy.lock']
state.setdefault('idle', {})['lock'] = idle
tmp = path + '.lock-next'
with open(tmp, 'w') as f:
    json.dump(state, f, indent=2); f.write('\n')
st = os.stat(path)
os.chown(tmp, st.st_uid, st.st_gid); os.chmod(tmp, 0o644)
os.replace(tmp, path)
PY

python3 - "$bindings" <<'PY'
import os, re, sys
path = sys.argv[1]
text = open(path).read()
block = '''-- >>> tablet.lock (shell-plugin/deploy-lock.sh)
-- Power key: lock (when a PIN is set) and screen off; again: screen on.
hl.unbind("XF86PowerOff")
o.bind("XF86PowerOff", "Lock and screen off", "/opt/omarchy-tablet/lock/tablet-power-key", { locked = true })
-- <<< tablet.lock
'''
text = re.sub(r'-- >>> tablet\.lock.*?-- <<< tablet\.lock\n', '', text, flags=re.S)
text = text.rstrip('\n') + '\n\n' + block
tmp = path + '.lock-next'
open(tmp, 'w').write(text)
st = os.stat(path)
os.chown(tmp, st.st_uid, st.st_gid); os.chmod(tmp, st.st_mode & 0o777)
os.replace(tmp, path)
PY
echo "backup: $backup"
REMOTE
