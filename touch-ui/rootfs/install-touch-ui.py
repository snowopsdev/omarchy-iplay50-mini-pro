#!/usr/bin/python
"""Install user-owned native touch controls. Run on the tablet as root.

Call only after the matching keyboard binary and tablet-ui helper are installed.
No packaged Omarchy file, Hyprland Lua, GPU environment, idle policy or service is
modified. Existing JSON outside the tablet bar layout is preserved.
"""
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import pwd
import shutil

account = pwd.getpwnam('hypr')
home = Path(account.pw_dir)
config = home / '.config/omarchy/shell.json'
module = home / '.config/omarchy/bar/modules/TabletButton.qml'
assets = Path(__file__).resolve().parent
backup = Path('/opt/omarchy-tablet/backups') / ('touch-ui-' + datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S%fZ'))
backup.mkdir(mode=0o700, parents=True)
for path in (config, module):
    if path.exists():
        shutil.copy2(path, backup / path.name)
state = json.loads(config.read_text())
layout = state.setdefault('bar', {}).setdefault('layout', {})

def button(name, label, command, description):
    return {'id': 'tablet.' + name, 'type': 'qml', 'text': label,
            'description': description, 'onClick': command,
            'source': str(module)}

# Keep native/custom status widgets, replacing only this port's prior buttons.
left = [entry for entry in layout.get('left', []) if not entry.get('id', '').startswith('tablet.')]
right = [entry for entry in layout.get('right', []) if not entry.get('id', '').startswith('tablet.')]
# Option A: the tablet.touch shell plugin's dock and handle provide Apps and
# window switching, so the bar keeps native status widgets plus Keyboard.
# button() remains for a quick revert to the pre-dock bar.
# One exception: a keyboard toggle stays in the bar, the only control that is
# always on screen while the keys are up (dock and handle step aside).
layout['left'] = left
layout['right'] = [button('keyboard', '\U000F030C', '/opt/omarchy-tablet/ui/tablet-ui keyboard',
                          'Show or minimize the touch keyboard')] + right

module.parent.mkdir(parents=True, exist_ok=True)
for path, content in ((module, (assets / 'TabletButton.qml').read_text()),
                      (config, json.dumps(state, indent=2) + '\n')):
    temporary = path.with_name(path.name + '.tablet-next')
    temporary.write_text(content)
    temporary.chmod(0o644)
    os.chown(temporary, account.pw_uid, account.pw_gid)
    temporary.replace(path)
print('Installed native touch bar (status + keyboard toggle); backup:', backup)
