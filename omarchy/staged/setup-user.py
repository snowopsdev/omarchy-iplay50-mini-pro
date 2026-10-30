#!/usr/bin/env python3
"""Apply tablet-owned overrides after seeding the packaged /etc/skel."""
import json
from pathlib import Path
import shutil
import subprocess

home = Path('/home/hypr')
# This vendor kernel's device filesystem lacks POSIX ACL support, so logind's
# normal uaccess grant cannot provide ALSA access. Membership applies on login.
subprocess.run(['usermod', '-aG', 'audio', 'hypr'], check=True)
config = home / '.config'
stage = Path('/opt/omarchy-tablet/omarchy-install/staged')
for name in ['monitors.lua', 'looknfeel.lua', 'autostart.lua']:
    shutil.copy2(stage / name, config / 'hypr' / name)
theme = config / 'omarchy/themes/tokyo-night'
theme.mkdir(parents=True, exist_ok=True)
shutil.copy2(stage / 'shell.toml', theme / 'shell.toml')
shell_path = config / 'omarchy/shell.json'
shell = json.loads(shell_path.read_text())
module_dir = config / 'omarchy/bar/modules'
module_dir.mkdir(parents=True, exist_ok=True)
shutil.copy2(stage / 'TabletButton.qml', module_dir / 'TabletButton.qml')
# The prototype account has no password yet; do not start an unusable lock.
shell['idle'] = {'screensaver': 0, 'lock': 0}
shell['bar']['centerAnchor'] = ''
shell['bar']['layout'] = {
    'left': [
        {'id': 'omarchy.menu'},
        {'id': 'tablet.apps', 'type': 'command', 'text': 'Apps', 'fontSize': 16,
         'horizontalMargin': 12, 'onClick': 'omarchy menu toggle apps'},
        {'id': 'tablet.rotate', 'type': 'command', 'text': 'Rotate',
         'onClick': '/opt/omarchy-tablet/tablet-orientation toggle'},
        {'id': 'tablet.auto', 'type': 'command', 'text': 'Auto',
         'onClick': '/opt/omarchy-tablet/tablet-orientation auto'},
    ],
    'center': [{'id': 'omarchy.clock', 'format': 'HH:mm'}],
    'right': [
        {'id': 'tablet.keyboard', 'type': 'command', 'text': 'Keys', 'fontSize': 16,
         'horizontalMargin': 12, 'onClick': '/opt/omarchy-tablet/ui/tablet-ui keyboard'},
        {'id': 'tablet.terminal', 'type': 'command', 'text': 'Term', 'fontSize': 16,
         'horizontalMargin': 12, 'onClick': 'omarchy launch terminal'},
        {'id': 'tablet.close', 'type': 'command', 'text': 'Close', 'fontSize': 16,
         'horizontalMargin': 12, 'onClick': '/opt/omarchy-tablet/ui/tablet-ui close'},
        {'id': 'omarchy.audio'},
        {'id': 'omarchy.network'},
        {'id': 'omarchy.power'},
    ],
}
# A custom QML widget follows the documented writable injection contract and
# avoids the packaged command widget's read-only moduleName warning.
for entries in shell['bar']['layout'].values():
    for entry in entries:
        if entry.get('type') == 'command':
            entry.update(type='qml', source=str(module_dir / 'TabletButton.qml'))
shell_path.write_text(json.dumps(shell, indent=2) + '\n')
# Preserve the packaged foot theme while increasing its font for the panel.
foot = config / 'foot/foot.ini'
foot.write_text(foot.read_text().replace('font=JetBrainsMono Nerd Font:size=9',
                                        'font=JetBrainsMono Nerd Font:size=13'))
