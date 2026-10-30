#!/usr/bin/env python3
"""Opt-in decoder access after VCU readiness; fail closed, never enable browser flags."""
import argparse
import hashlib
import json
import os
import platform
import re
import stat
import subprocess
import sys
import tempfile
from pathlib import Path

BASE = Path('/opt/omarchy-tablet/video')
ENABLE = BASE / 'browser-enabled'
READY = Path('/run/omarchy-tablet-video-ready')
MODULE = BASE / 'modules/mtk-vcodec-dec-v1-dmabuf-only.ko'
MODULE_SHA = '0a9643e1ec91ea1631841f14ae5b223fa7be00d745b9e24df3740d925f76ffca'
GUARD = BASE / 'probes/check-memory-guard'
HEAP = Path('/dev/dma_heap/mtk_mm')
UNIT = 'omarchy-tablet-vcu.service'
CLEAN_ENV = {'PATH': '/usr/sbin:/usr/bin:/sbin:/bin', 'LANG': 'C'}


def require_root_file(path):
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o022:
        raise RuntimeError(f'not a root-owned regular file protected from other writers: {path}')


def decoder_nodes():
    for entry in Path('/sys/class/video4linux').glob('video*'):
        if any(parent.name == '16000000.vdec' for parent in entry.resolve().parents):
            yield Path('/dev') / entry.name


def require_character(path):
    if not stat.S_ISCHR(path.lstat().st_mode):
        raise RuntimeError(f'expected a direct character device: {path}')


def permission(path, group, mode):
    require_character(path)
    os.chown(path, 0, group, follow_symlinks=False)
    os.chmod(path, mode, follow_symlinks=False)


def lock():
    # Remove the public readiness assertion before withdrawing access.
    READY.unlink(missing_ok=True)
    failures = []
    for path in [*decoder_nodes(), HEAP]:
        try:
            if path.exists() or path.is_symlink():
                permission(path, 0, 0o600)
        except Exception as error:
            failures.append(str(error))
    if failures:
        raise RuntimeError('; '.join(failures))


def service_ready():
    result = subprocess.run(['/usr/bin/systemctl', 'show', UNIT,
                             '-p', 'ActiveState', '-p', 'SubState', '-p', 'MainPID'],
                            env=CLEAN_ENV, text=True, capture_output=True, timeout=3, check=True)
    props = dict(line.split('=', 1) for line in result.stdout.splitlines() if '=' in line)
    state = (props.get('ActiveState'), props.get('SubState'))
    # Type=notify starts ExecStartPost only after the supervisor sends READY.
    if state not in (('active', 'running'), ('activating', 'start-post')):
        raise RuntimeError(f'VCU service has not completed notify readiness: {state}')
    pid = int(props.get('MainPID', '0'))
    if pid <= 1 or str(BASE / 'runtime/supervisor.py').encode() not in Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0'):
        raise RuntimeError('VCU MainPID does not identify the installed supervisor')
    return pid


def validate_guard(text, returncode):
    """Validate the complete zero-count live memory-mode contract, not disk identity."""
    pattern = re.compile(r'ENTER REQBUFS type=(\d+) memory=(MMAP|DMABUF) count=0\n'
                         r'RETURN ret=(-?\d+) errno=(\d+) count=(\d+) capabilities=([0-9a-fA-F]+)')
    rows = pattern.findall(text)
    expected = [(10, 'MMAP', -1, 22, 0, 0), (10, 'DMABUF', 0, 0, 0, 0x14),
                (9, 'MMAP', -1, 22, 0, 0), (9, 'DMABUF', 0, 0, 0, 0x14)]
    actual = [(int(t), mem, int(ret), int(err), int(count), int(caps, 16))
              for t, mem, ret, err, count, caps in rows]
    if (returncode or actual != expected or
        'GUARD PASS: both queues reject MMAP and advertise DMABUF; all requests count=0' not in text or
        'RETURN close; result=0' not in text):
        raise RuntimeError('live decoder does not prove the exact DMABUF-only zero-count contract')
    return {'output': {'mmap_errno': 22, 'dmabuf_caps': '0x00000014'},
            'capture': {'mmap_errno': 22, 'dmabuf_caps': '0x00000014'}}


def publish(record):
    # Public /run marker is separate from the private 0700 runtime root.
    with tempfile.NamedTemporaryFile(mode='w', dir=READY.parent,
                                     prefix=READY.name + '.', delete=False) as stream:
        temporary = Path(stream.name)
        try:
            json.dump(record, stream, sort_keys=True)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
            os.chmod(temporary, 0o644)
            os.replace(temporary, READY)
        finally:
            temporary.unlink(missing_ok=True)


def grant():
    lock()
    if not ENABLE.exists() and not ENABLE.is_symlink():
        print('VCU browser access disabled; decoder and heap remain root-only', flush=True)
        return
    try:
        if platform.system() != 'Linux' or platform.machine() not in ('aarch64', 'arm64'):
            raise RuntimeError('requires the native ARM64 target tablet')
        if b'MT8781' not in Path('/proc/device-tree/model').read_bytes():
            raise RuntimeError('device tree does not identify the expected MT8781 tablet')
        require_root_file(ENABLE)
        supervisor = service_ready()
        require_root_file(MODULE)
        if hashlib.sha256(MODULE.read_bytes()).hexdigest() != MODULE_SHA:
            raise RuntimeError('guarded decoder module disk identity mismatch')
        if not Path('/sys/module/mtk_vcodec_dec_v1').is_dir():
            raise RuntimeError('decoder module is not loaded')
        nodes = list(decoder_nodes())
        if len(nodes) != 1:
            raise RuntimeError(f'expected exactly one tablet decoder node, found {nodes}')
        for path in [nodes[0], HEAP]:
            require_character(path)
        require_root_file(GUARD)
        result = subprocess.run([str(GUARD), str(nodes[0])], env=CLEAN_ENV,
                                text=True, capture_output=True, timeout=8)
        print(result.stdout, end='', flush=True)
        if result.stderr:
            print(result.stderr, end='', file=sys.stderr, flush=True)
        caps = validate_guard(result.stdout, result.returncode)
        # Recheck both opt-in and service after probing. A cached disk hash does
        # not establish the live guard; the zero-count test above does.
        require_root_file(ENABLE)
        if service_ready() != supervisor:
            raise RuntimeError('VCU supervisor changed during access verification')
        import grp
        video_gid = grp.getgrnam('video').gr_gid
        permission(HEAP, video_gid, 0o660)
        permission(nodes[0], video_gid, 0o660)
        record = {'version': 1, 'boot_id': Path('/proc/sys/kernel/random/boot_id').read_text().strip(),
                  'guarded_module_sha256': MODULE_SHA, 'live_guard': caps,
                  'decoder': str(nodes[0]), 'heap': str(HEAP), 'supervisor_pid': supervisor}
        publish(record)
        print(f'VCU guarded browser access ready: {READY}', flush=True)
    except BaseException:
        lock()
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--grant', action='store_true')
    action.add_argument('--lock', action='store_true')
    args = parser.parse_args()
    if os.geteuid() != 0:
        raise RuntimeError('target root is required')
    if args.grant:
        grant()
    else:
        lock()
        print('VCU browser access withdrawn; decoder and heap root-only', flush=True)


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print(f'tablet-vcu-access: {error}', file=sys.stderr, flush=True)
        raise SystemExit(1)
