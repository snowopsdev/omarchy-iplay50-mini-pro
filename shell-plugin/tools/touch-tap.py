#!/usr/bin/python3
"""Inject touchscreen taps/swipes into the tablet's real touch device (root).

Coordinates are panel pixels (portrait 1200x1920 native panel space), so the
compositor applies its normal rotation and scaling, exactly as for a finger.
  touch-tap.py tap PX PY [hold_ms]
  touch-tap.py swipe PX0 PY0 PX1 PY1 [ms]
"""
import fcntl, glob, os, struct, sys, time

EV_SYN, EV_KEY, EV_ABS = 0, 1, 3
SYN_REPORT, BTN_TOUCH = 0, 0x14a
ABS_X, ABS_Y = 0x00, 0x01
ABS_MT_SLOT, ABS_MT_POSITION_X, ABS_MT_POSITION_Y, ABS_MT_TRACKING_ID = 0x2f, 0x35, 0x36, 0x39
ABS_MT_TOUCH_MAJOR, ABS_MT_PRESSURE = 0x30, 0x3a

def device():
    for path in sorted(glob.glob('/sys/class/input/event*/device/name')):
        if open(path).read().strip() == 'mtk-tpd':
            return '/dev/input/' + path.split('/')[4]
    raise SystemExit('mtk-tpd not found')

def absinfo(fd, code):
    buf = bytearray(24)
    fcntl.ioctl(fd, 0x80184540 + code, buf)  # EVIOCGABS(code)
    return struct.unpack('6i', buf)  # value min max fuzz flat res

def emit(fd, t, c, v):
    now = time.time()
    os.write(fd, struct.pack('qqHHi', int(now), int((now % 1) * 1e6), t, c, v))

def frame(fd, x, y, first=False):
    if first:
        emit(fd, EV_ABS, ABS_MT_SLOT, 0)
        emit(fd, EV_ABS, ABS_MT_TRACKING_ID, 4242)
    emit(fd, EV_ABS, ABS_MT_POSITION_X, x)
    emit(fd, EV_ABS, ABS_MT_POSITION_Y, y)
    emit(fd, EV_ABS, ABS_MT_TOUCH_MAJOR, 6)
    emit(fd, EV_ABS, ABS_MT_PRESSURE, 60)
    if first:
        emit(fd, EV_KEY, BTN_TOUCH, 1)
    emit(fd, EV_SYN, SYN_REPORT, 0)

def release(fd):
    emit(fd, EV_ABS, ABS_MT_SLOT, 0)
    emit(fd, EV_ABS, ABS_MT_TRACKING_ID, -1)
    emit(fd, EV_KEY, BTN_TOUCH, 0)
    emit(fd, EV_SYN, SYN_REPORT, 0)

def main():
    fd = os.open(device(), os.O_RDWR)
    sx = absinfo(fd, ABS_MT_POSITION_X)[2] / 1199.0
    sy = absinfo(fd, ABS_MT_POSITION_Y)[2] / 1919.0
    scale = lambda x, y: (round(x * sx), round(y * sy))
    cmd, args = sys.argv[1], [int(a) for a in sys.argv[2:]]
    if cmd == 'tap':
        x, y = scale(args[0], args[1])
        frame(fd, x, y, True)
        time.sleep((args[2] if len(args) > 2 else 60) / 1000)
        release(fd)
    elif cmd == 'swipe':
        (x0, y0), (x1, y1) = scale(args[0], args[1]), scale(args[2], args[3])
        ms = args[4] if len(args) > 4 else 250
        steps = max(6, ms // 16)
        frame(fd, x0, y0, True)
        for i in range(1, steps + 1):
            time.sleep(ms / 1000 / steps)
            frame(fd, round(x0 + (x1 - x0) * i / steps), round(y0 + (y1 - y0) * i / steps))
        release(fd)
    else:
        raise SystemExit(__doc__)

main()
