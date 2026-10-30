#!/usr/bin/env python3
"""Bounded, read-only sampling of VCU/watchdog scheduling during playback."""
import json
import sys
import time
from pathlib import Path

duration = int(sys.argv[1]) if len(sys.argv) > 1 else 600
assert 1 <= duration <= 1800
threads = []
for proc in Path('/proc').glob('[0-9]*'):
    try:
        if proc.joinpath('comm').read_text().strip() not in ('watchdogd', 'v3avpud'):
            continue
        for task in proc.joinpath('task').iterdir():
            name = task.joinpath('comm').read_text().strip()
            if name in ('watchdogd', 'receiver', 'vdec_srv'):
                threads.append((task, name))
    except OSError:
        continue
boot = Path('/proc/sys/kernel/random/boot_id').read_text().strip()
start = time.monotonic()
while time.monotonic() - start <= duration:
    row = {'elapsed': round(time.monotonic() - start, 3), 'boot': boot,
           'uptime': Path('/proc/uptime').read_text().split()[0], 'threads': []}
    for task, name in threads:
        try:
            fields = task.joinpath('stat').read_text().rsplit(')', 1)[1].split()
            row['threads'].append({'tid': int(task.name), 'name': name,
                'state': fields[0], 'cpu': int(fields[36]),
                'schedstat': [int(v) for v in task.joinpath('schedstat').read_text().split()]})
        except OSError:
            row['threads'].append({'tid': int(task.name), 'name': name, 'gone': True})
    row['mem_available_kb'] = next(int(x.split()[1]) for x in
        Path('/proc/meminfo').read_text().splitlines() if x.startswith('MemAvailable:'))
    print(json.dumps(row), flush=True)
    time.sleep(2)
