#!/usr/bin/env python3
"""Resolve the installed Omarchy manifest without changing installed packages."""
from pathlib import Path
import re
import subprocess

out = Path('/opt/omarchy-tablet/omarchy-install')
manifest = Path('/usr/share/omarchy/install/omarchy-base.packages')
packages = [s for line in manifest.read_text().splitlines()
            if (s := line.split('#', 1)[0].strip())]
missing = []
while True:
    result = subprocess.run(['pacman', '-Sp', '--needed', '--noconfirm',
                             '--print-format', '%n', '--', *packages],
                            text=True, capture_output=True)
    unavailable = re.findall(r'error: target not found: (\S+)', result.stderr)
    if not unavailable:
        break
    missing.extend(unavailable)
    packages = [p for p in packages if p not in unavailable]
out.joinpath('defaults-available.txt').write_text('\n'.join(packages) + '\n')
out.joinpath('defaults-unavailable.txt').write_text('\n'.join(missing) + '\n')
out.joinpath('defaults-resolution.log').write_text(result.stdout + result.stderr)
print(f'{len(packages)} manifest entries resolvable, {len(missing)} unavailable')
print('Unavailable:', ', '.join(missing))
if result.returncode:
    print(result.stderr)
    raise SystemExit(result.returncode)
