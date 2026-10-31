#!/usr/bin/python3
"""Bridge the vendor GPS daemon (mnld) to a standard NMEA pseudo-terminal.

Speaks the protocol of the vendor HAL module gps.default.so (reconstructed
from its exported hal2mnl_* senders): abstract AF_UNIX datagram sockets
"mtk_hal2mnl" (to mnld) and "mtk_mnl2hal" (from mnld), names zero-padded to
108 bytes. Messages are little-endian: int32 version (1), int32 command, args.
  101 init, 102 start (+float32 1.4 as the HAL sends), 103 stop,
  105 inject time (int64 UTC ms, int64 boot-clock ms, int32 uncertainty ms).
mnld sends NMEA sentences as message type 104.

NMEA goes to a pty; /run/tablet-gps/nmea links to it, for gpsd or any NMEA
reader. The GPS engine runs while this bridge runs; stop it to save power.
"""
import os, signal, socket, struct, sys, time, tty

RUN = "/run/tablet-gps"
LINK = RUN + "/nmea"

def addr(name):
    return b"\0" + name.encode() + b"\0" * (107 - len(name))

i32 = lambda v: struct.pack("<i", v)
i64 = lambda v: struct.pack("<q", v)

rx = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
rx.bind(addr("mtk_mnl2hal"))
rx.settimeout(1.0)
tx = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)

def send(*fields):
    try:
        tx.sendto(b"".join(fields), addr("mtk_hal2mnl"))
        return True
    except OSError as e:
        print("send failed: %s" % e, file=sys.stderr, flush=True)
        return False

def start():
    send(i32(1), i32(101))
    time.sleep(0.3)
    now_ms = int(time.time() * 1000)
    boot_ms = int(time.clock_gettime(time.CLOCK_BOOTTIME) * 1000)
    send(i32(1), i32(105), i64(now_ms), i64(boot_ms), i32(1000))
    send(i32(1), i32(102), struct.pack("<f", 1.4))
    print("GPS start requested", flush=True)

running = True
def stop(*_):
    global running
    running = False
signal.signal(signal.SIGTERM, stop)
signal.signal(signal.SIGINT, stop)

os.makedirs(RUN, exist_ok=True)
master, slave = os.openpty()
tty.setraw(slave)
os.chmod(os.ttyname(slave), 0o660)     # gpsd runs as nobody:uucp
try:
    import grp
    os.chown(os.ttyname(slave), 0, grp.getgrnam("uucp").gr_gid)
except (KeyError, OSError):
    pass
try:
    os.unlink(LINK)
except FileNotFoundError:
    pass
os.symlink(os.ttyname(slave), LINK)
os.set_blocking(master, False)

start()
last_nmea = time.time()
fix_reported = False
while running:
    try:
        d = rx.recv(65536)
    except socket.timeout:
        d = b""
    except InterruptedError:
        continue
    if len(d) >= 8 and struct.unpack_from("<i", d, 4)[0] == 104:
        text = d[8:]
        k = text.find(b"$")
        if k >= 0:
            line = text[k:].split(b"\0")[0].strip()
            last_nmea = time.time()
            try:
                os.write(master, line + b"\r\n")
            except BlockingIOError:
                pass               # nobody reading; drop
            if not fix_reported and line[3:6] == b"GGA" and line.split(b",")[6:7] not in ([b"0"], [b""]):
                print("first fix: %s" % line.decode(errors="replace"), flush=True)
                fix_reported = True
    try:                           # discard anything a reader writes back
        os.read(master, 4096)
    except (BlockingIOError, OSError):
        pass
    if running and time.time() - last_nmea > 15:
        print("no NMEA for 15 s (mnld restarted?), restarting GPS", flush=True)
        start()
        last_nmea = time.time()

send(i32(1), i32(103))
print("GPS stop sent", flush=True)
try:
    os.unlink(LINK)
except FileNotFoundError:
    pass
