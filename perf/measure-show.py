#!/usr/bin/python3
"""Home and Open apps opened over IPC, timed with the plugin's latency probes
(epoch ms). Home: state change -> first frame swapped (the shell's own work)
and IPC request -> first frame. Open apps: IPC request -> first frame. The IPC
figures include ~0.1 s of `quickshell ipc` start-up. No touch input; run on
the tablet as root. usage: measure-show.py RUNS"""
import json, subprocess, sys, time
ENV = ['runuser', '-u', 'hypr', '--', 'env', 'XDG_RUNTIME_DIR=/run/user/1001',
       'WAYLAND_DISPLAY=wayland-1', 'quickshell', 'ipc', '-p',
       '/opt/omarchy-tablet/omarchy-runtime/shell', 'call', 'shell']
def call(*args):
    return subprocess.run(ENV + list(args), capture_output=True, text=True).stdout
def debug():
    return json.loads(call('call', 'tablet.touch', 'debug', ''))
res = {'home: change -> frame': [], 'home: ipc -> frame': [], 'switcher: ipc -> frame': []}
for i in range(int(sys.argv[1])):
    for view, key in (('home', 'homeFrameAt'), ('switcher', 'switcherFrameAt')):
        call('hide', 'tablet.touch'); time.sleep(1.5)
        before = debug()[key]
        t0 = time.time() * 1000
        call('summon', 'tablet.touch', json.dumps({'view': view}))
        for _ in range(60):
            time.sleep(0.02)
            d = debug()
            if d[key] != before:
                res[view + ': ipc -> frame'].append(d[key] - t0)
                if view == 'home':
                    res['home: change -> frame'].append(d['homeFrameAt'] - d['homeChangedAt'])
                break
        time.sleep(0.5)
call('hide', 'tablet.touch')
for name, v in res.items():
    v.sort()
    print(name, ' '.join('%.0f' % x for x in v), 'ms; median %.0f' % v[len(v) // 2] if v else 'no frames')
