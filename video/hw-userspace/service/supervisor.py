#!/usr/bin/env python3
"""Root-only native VCU supervisor. Never streams video or changes browser flags."""
import os
import hashlib
import signal
import socket
import stat
import subprocess
import sys
import time
from pathlib import Path

BASE = Path('/opt/omarchy-tablet/video')
RUNTIME = BASE / 'runtime'
QUERY = BASE / 'probes/v4l2-query'
BROWSER_READY = Path('/run/omarchy-tablet-video-ready')
VIDEO_HEAP = Path('/dev/dma_heap/mtk_mm')
STOPPING = False


def notify(message):
    address = os.environ.get('NOTIFY_SOCKET')
    if address:
        if address.startswith('@'):
            address = '\0' + address[1:]
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as channel:
            channel.connect(address)
            channel.sendall(message.encode())


def descendants(pid):
    # CONFIG_CHECKPOINT_RESTORE is off: /proc/PID/task/PID/children is absent.
    # Build a host-visible PPid map from ordinary proc stat records instead.
    children = {}
    for entry in Path('/proc').iterdir():
        if not entry.name.isdigit():
            continue
        try:
            record = entry.joinpath('stat').read_text()
            fields = record[record.rindex(')') + 2:].split()
            children.setdefault(int(fields[1]), []).append(int(entry.name))
        except (FileNotFoundError, ProcessLookupError):
            continue
    result = []
    todo = [pid]
    while todo:
        current = todo.pop()
        result.append(current)
        todo.extend(children.get(current, []))
    return result


def daemon_receivers(parent_pid):
    for pid in descendants(parent_pid):
        try:
            proc = Path(f'/proc/{pid}')
            if proc.joinpath('comm').read_text().strip() != 'v3avpud':
                continue
            ns = proc.joinpath('ns/pid').readlink()
            if ns == Path('/proc/self/ns/pid').readlink():
                raise RuntimeError('vendor daemon escaped the required private PID namespace')
            namespace_ids = next(line for line in proc.joinpath('status').read_text().splitlines()
                                 if line.startswith('NSpid:')).split()[1:]
            if int(namespace_ids[-1]) >= 65536:
                raise RuntimeError('Bionic namespace TID exceeds 16-bit mutex ABI')
            receivers = 0
            worker_tids = set()
            for thread in proc.joinpath('task').iterdir():
                name = thread.joinpath('comm').read_text().strip()
                if name in ('vdec_srv', 'venc_srv'):
                    ids = next(line for line in thread.joinpath('status').read_text().splitlines()
                               if line.startswith('NSpid:')).split()[1:]
                    worker_tids.add(int(ids[-1]))
                if name != 'receiver':
                    continue
                args = thread.joinpath('syscall').read_text().split()
                # ARM32 ioctl(fd, VCU_GET_OBJECT, ptr), observed exact ABI.
                # syscall index is architecture-specific; inspect request arg.
                if len(args) >= 3 and int(args[2], 0) == 0xc058760a:
                    receivers += 1
            # Exact startup order: receiver threads are created BEFORE the
            # initial VCU_SET_OBJECT. Main only joins a codec service thread
            # AFTER md32_ipi_send(initial signaled=1/fw=0.2.14) returns.
            # A join uses shared FUTEX_WAIT with expected value = worker TID;
            # early pthread startup locks have a different value/private op.
            main_args = proc.joinpath('syscall').read_text().split()
            main_joined = (len(main_args) >= 4 and int(main_args[0], 0) in (240, 422)
                           and int(main_args[2], 0) == 0
                           and int(main_args[3], 0) in worker_tids)
            if receivers >= 2 and len(worker_tids) == 2 and main_joined:
                return pid
        except (FileNotFoundError, ProcessLookupError, StopIteration):
            continue
    return None


def decoder_nodes():
    for entry in Path('/sys/class/video4linux').glob('video*'):
        # Exact platform device observed on this tablet; do not lock cameras.
        if any(parent.name == '16000000.vdec' for parent in entry.resolve().parents):
            device = Path('/dev') / entry.name
            if device.exists() and stat.S_ISCHR(device.stat().st_mode):
                yield device


def lock_decoders():
    BROWSER_READY.unlink(missing_ok=True)
    nodes = list(decoder_nodes())
    for node in nodes:
        os.chown(node, 0, 0)
        os.chmod(node, 0o600)
    if VIDEO_HEAP.exists() or VIDEO_HEAP.is_symlink():
        if not stat.S_ISCHR(VIDEO_HEAP.lstat().st_mode):
            raise RuntimeError('video heap is not a direct character device')
        os.chown(VIDEO_HEAP, 0, 0, follow_symlinks=False)
        os.chmod(VIDEO_HEAP, 0o600, follow_symlinks=False)
    return nodes


def terminate(_signum, _frame):
    global STOPPING
    STOPPING = True


def main():
    if os.geteuid() != 0:
        raise RuntimeError('target root is required')
    subprocess.run([str(RUNTIME / 'native-arm-policy.py'), '--check'], check=True)
    if not QUERY.is_file():
        raise RuntimeError(f'validated query helper missing: {QUERY}')
    signal.signal(signal.SIGTERM, terminate)
    signal.signal(signal.SIGINT, terminate)
    child = subprocess.Popen([
        '/usr/bin/unshare', '--mount', '--pid', '--mount-proc',
        '--propagation', 'private', '--fork', '--kill-child=KILL',
        str(RUNTIME / 'runtime-inner.sh')])
    try:
        deadline = time.monotonic() + 20
        daemon = None
        while not STOPPING and time.monotonic() < deadline:
            if child.poll() is not None:
                raise RuntimeError(f'vendor runtime exited during bootstrap: {child.returncode}')
            daemon = daemon_receivers(child.pid)
            if daemon:
                break
            time.sleep(0.025)
        if not daemon or STOPPING:
            raise RuntimeError('native VCU receiver readiness deadline expired or service stopped')
        # The unsafe unadapted MMAP API must remain unavailable to desktop apps.
        if not Path('/sys/module/mtk_vcodec_dec_v1').exists():
            guarded = BASE / 'modules/mtk-vcodec-dec-v1-dmabuf-only.ko'
            expected = '0a9643e1ec91ea1631841f14ae5b223fa7be00d745b9e24df3740d925f76ffca'
            if hashlib.sha256(guarded.read_bytes()).hexdigest() != expected:
                raise RuntimeError('private DMABUF-only decoder module identity mismatch')
            subprocess.run(['/usr/bin/insmod', str(guarded)], check=True)
        subprocess.run(['/usr/bin/udevadm', 'settle', '--timeout=5'], check=True)
        nodes = lock_decoders()
        if len(nodes) != 1:
            raise RuntimeError(f'expected one tablet decoder node, found {nodes}')
        probe = subprocess.run([str(QUERY), str(nodes[0])], text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10)
        print(probe.stdout, end='', flush=True)
        if (probe.returncode or
            'CAPS driver=mtk-vcodec-dec card=platform:mt6789' not in probe.stdout or
            'FORMAT queue=10 index=0 fourcc=H264' not in probe.stdout or
            'FORMAT queue=9 index=0 fourcc=M21S' not in probe.stdout or
            'RETURN close ret=0' not in probe.stdout):
            raise RuntimeError('VCU-backed format enumeration/close did not pass')
        # The query can briefly wake receivers. Require them to return to
        # their protocol waits, rather than racing a single proc snapshot.
        post_query_deadline = time.monotonic() + 2
        while not STOPPING and not daemon_receivers(child.pid):
            if child.poll() is not None or time.monotonic() >= post_query_deadline:
                raise RuntimeError('VCU startup state did not settle after readiness query')
            time.sleep(0.025)
        if STOPPING:
            raise RuntimeError('service stopped during readiness query')
        lock_decoders()
        notify('READY=1\nSTATUS=Native VCU ready; browser access controlled by explicit opt-in')
        while not STOPPING and child.poll() is None:
            time.sleep(0.2)
        if not STOPPING:
            raise RuntimeError(f'vendor runtime exited: {child.returncode}')
        notify('STOPPING=1\nSTATUS=Stopping native VCU runtime')
    finally:
        try:
            lock_decoders()
        finally:
            # A permission/marker failure must not skip namespace teardown.
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=3)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as error:
        print(f'tablet-vcu: {error}', file=sys.stderr, flush=True)
        sys.exit(1)
