#!/usr/bin/python3
"""Time a real-touch tap on the dock's Open apps button until Hyprland maps
the tablet-switcher layer (socket2 openlayer event). Taps again to close.
usage: measure-open.py RUNS"""
import os, socket, subprocess, sys, time
runtime = '/run/user/1001'
sig = sorted(os.scandir(runtime + '/hypr'), key=lambda d: d.stat().st_mtime)[-1].name
s = socket.socket(socket.AF_UNIX); s.connect('%s/hypr/%s/.socket2.sock' % (runtime, sig))
s.settimeout(3)
tap = ['python3', '/opt/omarchy-tablet/diagnostics/touch-tap.py', 'tap', '1118', '737', '40']
def wait(event, t0):
    buf = b''
    while True:
        buf += s.recv(4096)
        for line in buf.split(b'\n'):
            if line.strip() == event:
                return time.monotonic() - t0
out = []
for i in range(int(sys.argv[1])):
    time.sleep(2.5)                      # let the clocks fall back to idle
    t0 = time.monotonic(); subprocess.run(tap)
    out.append(wait(b'openlayer>>tablet-switcher', t0) * 1000)
    time.sleep(0.6); t1 = time.monotonic(); subprocess.run(tap)
    wait(b'closelayer>>tablet-switcher', t1)
print(' '.join('%.0f' % x for x in out), 'ms; median %.0f' % sorted(out)[len(out) // 2])
