#!/usr/bin/env python3
"""Check/install native ARM32 execution policy, preserving other binfmt rules.
Run only on the target ARM64 tablet. --install writes explicit /etc overrides
and unregisters ONLY qemu-arm. Defaults to read-only --check.
"""
import argparse
import hashlib
import json
import os
import platform
import time
from pathlib import Path

DIRECTORIES = [Path('/etc/binfmt.d'), Path('/run/binfmt.d'),
               Path('/usr/local/lib/binfmt.d'), Path('/usr/lib/binfmt.d')]
LIVE = Path('/proc/sys/fs/binfmt_misc/qemu-arm')


def is_arm_rule(line):
    stripped = line.strip()
    if not stripped or stripped[0] in '#;':
        return False
    fields = stripped.split(stripped[0])
    return len(fields) > 2 and fields[1] == 'qemu-arm'


def effective():
    configs = {}
    for directory in DIRECTORIES:
        for path in directory.glob('*.conf'):
            if path.name not in configs:
                configs[path.name] = path
    return configs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', action='store_true')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if args.install and args.check:
        parser.error('select one operation')
    if platform.system() != 'Linux' or platform.machine() not in ('aarch64', 'arm64') or os.geteuid() != 0:
        raise RuntimeError('requires root on the native ARM64 target tablet')
    if b'MT8781' not in Path('/proc/device-tree/model').read_bytes():
        raise RuntimeError('device tree does not identify the expected MT8781 tablet')
    matching = []
    for name, path in sorted(effective().items()):
        text = path.read_text()
        if any(is_arm_rule(line) for line in text.splitlines()):
            matching.append((name, path, text))
    if not args.install:
        if matching:
            raise RuntimeError('qemu-arm remains configured in: ' + ', '.join(str(p) for _, p, _ in matching))
        if LIVE.exists() and LIVE.read_text().splitlines()[0] == 'enabled':
            raise RuntimeError('qemu-arm is active; native vendor ARM32 execution would be intercepted')
        print('Native ARM32 policy checked; other binfmt architectures untouched')
        return
    backup = Path('/opt/omarchy-tablet/backups') / ('native-arm-' + time.strftime('%Y%m%dT%H%M%SZ', time.gmtime()))
    backup.mkdir(parents=True, mode=0o700, exist_ok=False)
    records = []
    Path('/etc/binfmt.d').mkdir(parents=True, exist_ok=True)
    for name, source, original in matching:
        target = Path('/etc/binfmt.d') / name
        prior = {'existed': target.exists() or target.is_symlink()}
        if prior['existed']:
            if target.is_symlink():
                prior['symlink'] = os.readlink(target)
            else:
                saved = backup / name
                saved.write_bytes(target.read_bytes())
                saved.chmod(0o600)
                prior['backup'] = str(saved)
        rewritten = ''.join(('# Disabled only qemu-arm: native ARM32 vendor ABI\n' if is_arm_rule(line) else line)
                            for line in original.splitlines(keepends=True))
        temporary = target.with_name(target.name + '.tablet-tmp')
        with temporary.open('x') as stream:
            stream.write(rewritten)
        temporary.chmod(0o644)
        os.replace(temporary, target)
        records.append({'target': str(target), 'source': str(source),
                        'source_sha256': hashlib.sha256(original.encode()).hexdigest(), 'prior': prior})
    if LIVE.exists():
        registration = LIVE.read_text()
        if 'interpreter ' not in registration or 'qemu-arm' not in registration:
            raise RuntimeError('unexpected qemu-arm registration; refusing to remove it')
        (backup / 'live-registration.txt').write_text(registration)
        LIVE.write_text('-1\n')
    (backup / 'manifest.json').write_text(json.dumps(records, indent=2) + '\n')
    print(f'Native ARM32 override installed; other rules retained. Backup: {backup}')


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        raise SystemExit(f'native-arm-policy: {error}')
