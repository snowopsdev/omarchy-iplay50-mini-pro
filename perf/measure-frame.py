#!/usr/bin/python3
"""Real-touch tap on the dock's Open apps button -> first frame the shell
swaps for the panel (plugin probe, epoch ms). Measured from the touch-up write.
usage: measure-frame.py RUNS"""
import json, subprocess, sys, time
ENV = ['runuser', '-u', 'hypr', '--', 'env', 'XDG_RUNTIME_DIR=/run/user/1001',
       'WAYLAND_DISPLAY=wayland-1', 'quickshell', 'ipc', '-p',
       '/opt/omarchy-tablet/omarchy-runtime/shell', 'call', 'shell']
def debug():
    return json.loads(subprocess.run(ENV + ['call', 'tablet.touch', 'debug', ''],
                                     capture_output=True, text=True).stdout)
TAP = '/opt/omarchy-tablet/diagnostics/touch-tap.py'
sys.path.insert(0, '/opt/omarchy-tablet/diagnostics')
out = []
for i in range(int(sys.argv[1])):
    time.sleep(2.5)
    before = debug()['switcherFrameAt']
    up = time.time() * 1000
    subprocess.run(['python3', TAP, 'tap', '1118', '737', '1'])
    up = time.time() * 1000          # touch-up written just before exit
    for _ in range(40):
        time.sleep(0.05)
        d = debug()
        if d['switcherFrameAt'] != before:
            out.append(d['switcherFrameAt'] - up); break
    time.sleep(0.5)
    subprocess.run(ENV + ['hide', 'tablet.touch'], capture_output=True)
print(' '.join('%.0f' % x for x in out), 'ms; median %.0f' % sorted(out)[len(out) // 2])
