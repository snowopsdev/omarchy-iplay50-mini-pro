#!/usr/bin/env python3
"""Temporary MT8781 SDP current ceiling; preserve foreign thermal policy."""
import argparse
import contextlib
import fcntl
import json
import os
import platform
import re
import signal
import stat
import struct
import sys
import tempfile
import threading
from pathlib import Path

CAP = 500000
BUILD_IDS = {
    'mtk_charger_framework': '70015f58d2db49ada736c031c741c45b79824fdc',
    'sc8989x_charger': '582c1ccf43abc87c28709544cd108671406355ce',
}


def build_id(data):
    offset = 0
    while offset + 12 <= len(data):
        namesz, descsz, kind = struct.unpack_from('<III', data, offset)
        start = offset + 12
        desc = start + ((namesz + 3) & ~3)
        end = desc + descsz
        if end > len(data):
            break
        if data[start:start + namesz] == b'GNU\0' and kind == 3:
            return data[desc:end].hex()
        offset = desc + ((descsz + 3) & ~3)
    raise RuntimeError('missing or malformed GNU build ID')


class Controller:
    def __init__(self, root=Path('/'), owner_uid=0):
        # root/owner_uid support offline fixtures; the CLI always uses defaults.
        self.root, self.owner_uid = root, owner_uid
        self.directory = self.path('/run/omarchy-usb-current')
        self.state = self.directory / 'owned.json'
        self.policy = self.path('/sys/devices/platform/charger/input_current')
        self.target = self.path('/sys/class/power_supply/mtk-master-charger/input_current_limit')
        self.chip = self.path('/sys/class/power_supply/5-006a')

    def path(self, name):
        return self.root / name.lstrip('/')

    @staticmethod
    def log(message):
        print('usb-host-current: ' + message, flush=True)

    def protected(self, path, mode, directory=False):
        info = path.lstat()
        correct_type = stat.S_ISDIR(info.st_mode) if directory else stat.S_ISREG(info.st_mode)
        if not correct_type or info.st_uid != self.owner_uid or stat.S_IMODE(info.st_mode) != mode:
            raise RuntimeError(f'unsafe ownership, type or permissions: {path}')

    @contextlib.contextmanager
    def locked(self):
        self.directory.mkdir(mode=0o700, exist_ok=True)
        self.protected(self.directory, 0o700, directory=True)
        fd = os.open(self.directory / 'lock', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
        try:
            info = os.fstat(fd)
            if not stat.S_ISREG(info.st_mode) or info.st_uid != self.owner_uid or stat.S_IMODE(info.st_mode) != 0o600:
                raise RuntimeError('unsafe lock file')
            fcntl.flock(fd, fcntl.LOCK_EX)
            yield
        finally:
            os.close(fd)

    def boot(self):
        value = self.path('/proc/sys/kernel/random/boot_id').read_text().strip()
        if not re.fullmatch(r'[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}', value):
            raise RuntimeError('invalid boot ID')
        return value

    def verify(self):
        if self.path('/proc/device-tree/model').read_bytes().rstrip(b'\0') != b'MT8781V/CA':
            raise RuntimeError('unexpected tablet model')
        if b'mediatek,MT6789' not in self.path('/proc/device-tree/compatible').read_bytes().split(b'\0'):
            raise RuntimeError('unexpected tablet compatible')
        for module, expected in BUILD_IDS.items():
            note = self.path(f'/sys/module/{module}/notes/.note.gnu.build-id').read_bytes()
            if build_id(note) != expected:
                raise RuntimeError(f'loaded module identity mismatch: {module}')
        if (self.chip / 'manufacturer').read_text().strip() != 'SouthChip':
            raise RuntimeError('unexpected charger manufacturer')
        # Both controls must exist and parse before any policy write.
        int(self.policy.read_text().strip())
        if int(self.target.read_text().strip()) < 0:
            raise RuntimeError('charger hardware current getter unavailable')

    def load(self):
        if not self.state.exists() and not self.state.is_symlink():
            return None
        self.protected(self.state, 0o600)
        if self.state.stat().st_size > 4096:
            raise RuntimeError('oversize ownership record')
        record = json.loads(self.state.read_text())
        if (set(record) != {'version', 'boot_id', 'original_policy', 'cap'} or
                any(type(record[key]) is not int for key in ('version', 'original_policy', 'cap')) or
                record['version'] != 1 or record['original_policy'] != -1 or record['cap'] != CAP):
            raise RuntimeError('invalid ownership record')
        if record['boot_id'] != self.boot():
            self.state.unlink()
            self.log('discarded ownership from another boot; policy untouched')
            return None
        return record

    def save(self):
        record = {'version': 1, 'boot_id': self.boot(), 'original_policy': -1, 'cap': CAP}
        with tempfile.NamedTemporaryFile(mode='w', dir=self.directory, prefix='.owned-', delete=False) as stream:
            temporary = Path(stream.name)
            try:
                os.chmod(temporary, 0o600)
                json.dump(record, stream, sort_keys=True)
                stream.write('\n')
                stream.flush()
                os.fsync(stream.fileno())
                os.replace(temporary, self.state)
            finally:
                temporary.unlink(missing_ok=True)

    def source(self):
        online = int((self.chip / 'online').read_text().strip())
        if online == 0:
            return 'unknown'
        if online != 1:
            raise RuntimeError('invalid charger online value')
        kind = (self.chip / 'type').read_text().strip()
        selected = re.findall(r'\[([^\[\]]+)\]', (self.chip / 'usb_type').read_text())
        if len(selected) != 1:
            raise RuntimeError('cannot identify selected USB type')
        if selected[0] == 'SDP' and kind == 'USB':
            return 'sdp'
        if selected[0] in {'CDP', 'DCP', 'PD', 'PD_DRP', 'PD_PPS'} or kind in {'USB_CDP', 'USB_DCP', 'USB_PD', 'USB_PD_DRP'}:
            return 'other'
        return 'unknown'

    def change(self, expected, value):
        self.verify()
        if int(self.policy.read_text().strip()) != expected:
            return False
        self.write_policy(value)
        return int(self.policy.read_text().strip()) == value

    def write_policy(self, value):
        self.target.write_text(str(value) + '\n')

    def restore(self):
        if not self.load():
            return
        self.verify()
        if int(self.policy.read_text().strip()) == CAP:
            if not self.change(CAP, -1):
                raise RuntimeError('could not verify restoration of original policy')
            self.log('restored owned policy to -1')
        else:
            self.log('policy changed externally; relinquished ownership without a write')
        self.state.unlink(missing_ok=True)

    def step(self):
        self.verify()
        owned = self.load()
        policy = int(self.policy.read_text().strip())
        if owned and policy != CAP:
            self.state.unlink()
            self.log('policy changed externally; relinquished ownership without a write')
            return
        source = self.source()
        if owned:
            if source == 'other':
                self.restore()
            return  # Keep our cap across unknown/offline/VBUS transients.
        if source == 'sdp' and policy == -1:
            # Record intent before writing: SIGKILL after the write retains
            # recovery state. A pre-write crash still leaves policy -1 intact.
            self.save()
            if not self.change(-1, CAP):
                self.state.unlink(missing_ok=True)
                raise RuntimeError('policy changed during cap application')
            self.log('applied owned SDP ceiling 500000 microamps')

    def adopt(self):
        self.verify()
        if self.load():
            return
        if self.source() != 'sdp' or int(self.policy.read_text().strip()) != CAP:
            raise RuntimeError('adoption requires online SDP and existing policy 500000')
        self.save()
        self.log('adopted root trial cap with explicitly asserted original policy -1')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--once', action='store_true')
    action.add_argument('--restore', action='store_true')
    action.add_argument('--adopt-existing', action='store_true')
    parser.add_argument('--original-policy', type=int)
    args = parser.parse_args()
    if args.adopt_existing != (args.original_policy == -1) or (not args.adopt_existing and args.original_policy is not None):
        parser.error('adoption requires --adopt-existing --original-policy=-1; no other action accepts an original policy')
    if os.geteuid() != 0 or platform.system() != 'Linux' or platform.machine() not in {'aarch64', 'arm64'}:
        raise RuntimeError('native ARM64 Linux target root is required')
    controller = Controller()
    if args.once or args.restore or args.adopt_existing:
        with controller.locked():
            (controller.restore if args.restore else controller.adopt if args.adopt_existing else controller.step)()
        return
    stopped = threading.Event()
    signal.signal(signal.SIGTERM, lambda *_: stopped.set())
    signal.signal(signal.SIGINT, lambda *_: stopped.set())
    try:
        while not stopped.is_set():
            with controller.locked():
                controller.step()
            stopped.wait(2)
    finally:
        with controller.locked():
            controller.restore()


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(f'usb-host-current: {error}', file=sys.stderr, flush=True)
        sys.exit(1)
