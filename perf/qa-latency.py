#!/usr/bin/python3
"""End-to-end UI latency QA, measured on the real display (kmsgrab, ~60 Hz).

For each scenario: run its action as the desktop user (same environment as the
shell, so launches go through uwsm-app like a Home tap), then report
  first  = action -> first changed frame on screen
  settle = action -> last changed frame (screen stopped changing for QUIET s)
and undo the action. Run as root on the tablet:
  qa-latency.py [RUNS] [SCENARIO ...]        (default: 3 runs, all scenarios)
Scenarios named *-over-app first open Chromium (QA_BACKDROP desktop id) and
measure the shell over it; the browser is closed afterwards.
"""
import json, os, re, subprocess, sys, threading, time

W, H = 120, 192                  # downscaled grey frame (portrait panel 1200x1920)
QUIET = 0.8                      # s without change => settled
LIMIT = 8.0                      # s max per action
SHELL = '/opt/omarchy-tablet/omarchy-runtime/shell'
UI = '/opt/omarchy-tablet/ui/tablet-ui'


def session_env():
    pid = subprocess.run(['pgrep', '-u', 'hypr', '-x', 'quickshell'],
                         capture_output=True, text=True).stdout.split()[0]
    env = dict(kv.split('=', 1) for kv in
               open(f'/proc/{pid}/environ').read().split('\0') if '=' in kv)
    env.pop('LD_PRELOAD', None)      # app-env re-adds what apps need
    return env


ENV = session_env()


def user(cmd, wait=True):
    full = ['runuser', '-u', 'hypr', '--', 'env', '-i'] + \
        [f'{k}={v}' for k, v in ENV.items()] + cmd
    if wait:
        return subprocess.run(full, capture_output=True, text=True, cwd='/home/hypr').stdout
    return subprocess.Popen(full, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            cwd='/home/hypr')


def hypr(*args):
    return user(['hyprctl'] + list(args))


def clients():
    return json.loads(hypr('clients', '-j') or '[]')


def shell(*args):
    return user(['quickshell', 'ipc', '-p', SHELL, 'call', 'shell'] + list(args))


def launch(desktop):
    return lambda: user(['uwsm-app', '--', 'gtk-launch', desktop], wait=False)


def close_new(before):
    def undo():
        old = {c['address'] for c in before}
        for c in clients():
            if c['address'] not in old:
                hypr('dispatch', f'hl.dsp.window.close({{window="address:{c["address"]}"}})')
        time.sleep(1.5)
    return undo


class Capture:
    def __init__(self):
        self.frames = []             # (wall time, bytes)
        self.p = subprocess.Popen(
            ['ffmpeg', '-hide_banner', '-nostats', '-loglevel', 'info',
             '-device', '/dev/dri/card0', '-f', 'kmsgrab', '-framerate', '60', '-i', '-',
             '-copyts', '-vf', f'hwdownload,format=bgr0,scale={W}:{H},format=gray,showinfo',
             # passthrough: no frame dup/drop, so stdout frames pair 1:1 with showinfo
             '-fps_mode', 'passthrough', '-f', 'rawvideo', '-'],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.times, self.start = [], None
        threading.Thread(target=self._err, daemon=True).start()
        threading.Thread(target=self._out, daemon=True).start()

    def _err(self):
        for line in self.p.stderr:
            line = line.decode(errors='replace')
            m = re.search(r'start (\d+\.\d+)', line)
            if m and self.start is None:
                self.start = float(m.group(1))
            m = re.search(r'pts_time:(\S+)', line)
            if m:
                self.times.append(float(m.group(1)))

    def _out(self):
        n = W * H
        while True:
            b = self.p.stdout.read(n)
            if len(b) < n:
                return
            self.frames.append(b)

    def stamped(self):
        k = min(len(self.frames), len(self.times))
        # kmsgrab pts are wall-clock µs; with -copyts pts_time is absolute
        return [(self.times[i], self.frames[i]) for i in range(k)]

    def stop(self):
        self.p.terminate()
        self.p.wait()


def changed(a, b):
    diff = sum(1 for x, y in zip(a[::3], b[::3]) if abs(x - y) > 10)
    return diff > 20                 # ignore a blinking text cursor


def measure(cap, action):
    time.sleep(0.6)
    t0 = time.time()
    action()
    i, prev, first, last = 0, None, None, None
    while True:
        time.sleep(0.1)
        fr = cap.stamped()
        for t, b in fr[i:]:
            if t >= t0 and prev is not None and changed(prev, b):
                first = first or t
                last = t
            prev = b
        i = len(fr)
        if not fr or fr[-1][0] < t0:
            continue
        now = fr[-1][0]
        if (last and now - last >= QUIET) or now - t0 > LIMIT:
            return (first and first - t0, last and last - t0)


def scenarios():
    s = {}
    hide = lambda: (shell('hide', 'tablet.touch'), time.sleep(1.0))
    for view in ('home', 'switcher', 'peek'):
        s[view + '-over-app'] = (
            (lambda v: lambda: shell('summon', 'tablet.touch', '{"view":"%s"}' % v))(view), hide)
    s['keyboard'] = (lambda: user([UI, 'keyboard-show']),
                     lambda: (user([UI, 'keyboard-hide']), time.sleep(0.8)))
    for name, desk in (('foot', 'foot.desktop'), ('files', 'org.gnome.Nautilus.desktop'),
                       ('chromium', 'chromium.desktop'), ('calc', 'omacalc.desktop'),
                       ('youtube', 'YouTube.desktop')):
        s[name] = (launch(desk), None)
    s['chromium-warm-over-app'] = (launch('chromium.desktop'), None)
    s['youtube-warm-over-app'] = (launch('YouTube.desktop'), None)
    s['foot-direct'] = (lambda: hypr('dispatch', 'hl.dsp.exec_cmd("foot")'), None)
    s['foot-uwsm'] = (lambda: user(['uwsm-app', '--', 'foot'], wait=False), None)
    s['keyboard-sig'] = (lambda: subprocess.run(['pkill', '-USR2', '-x', 'wvkbd-mobintl']),
                         lambda: (subprocess.run(['pkill', '-USR1', '-x', 'wvkbd-mobintl']),
                                  time.sleep(0.8)))
    return s


def main():
    runs = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    want = sys.argv[2:] or list(scenarios())
    backdrop = None
    if any(n.endswith('-over-app') for n in want):
        backdrop = clients()
        launch(os.environ.get('QA_BACKDROP', 'chromium.desktop'))()
        time.sleep(10)
    cap = Capture()
    time.sleep(1.5)
    out = {}
    try:
        for name in want:
            act, undo = scenarios()[name]
            res = []
            for _ in range(runs):
                before = clients()
                res.append(measure(cap, act))
                (undo or close_new(before))()
            out[name] = res
            fmt = lambda v: '-' if v is None else '%4.0f' % (v * 1000)
            print('%-9s first ms: %s | settle ms: %s' % (
                name, ' '.join(fmt(r[0]) for r in res), ' '.join(fmt(r[1]) for r in res)),
                flush=True)
    finally:
        cap.stop()
        if backdrop is not None:
            close_new(backdrop)()
    json.dump(out, open('/tmp/qa-latency.json', 'w'))


main()
