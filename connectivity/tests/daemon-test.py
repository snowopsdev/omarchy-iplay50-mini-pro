#!/usr/bin/env python3
# Functional test of the daemon loop under qemu-user: pty stands in for /dev/stpwmt.  usage: daemon-test.py <test-binary> <fwdir> <tmpdir>
import os, pty, sys, time, subprocess, tty, select
binp, fw, tmp = sys.argv[1:4]
dev = os.path.join(tmp, "dev"); st = os.path.join(tmp, "state"); os.makedirs(dev, exist_ok=True)
m, s = pty.openpty(); tty.setraw(m); tty.setraw(s)
for n in ("wmtdetect", "stpwmt"):
    p = os.path.join(dev, n)
    if os.path.lexists(p): os.unlink(p)
    os.symlink(os.ttyname(s), p)
r = subprocess.run(["qemu-aarch64-static", binp, "-p", fw, "--devdir", dev, "--state", st, "--wait", "20"], capture_output=True, text=True)
print("parent rc", r.returncode); print(r.stderr[-1500:])
def txrx(cmd):
    os.write(m, cmd.encode()); out = b""; t = time.time()
    while time.time() - t < 3:
        if select.select([m], [], [], 0.3)[0]:
            out += os.read(m, 256)
            if out: break
    return out
print("srh_rom_patch ->", txrx("srh_rom_patch"))
print("bogus ->", txrx("bogus_cmd"))
pid = int(open(os.path.join(st, "mtk-wmt-init.pid")).read())
os.kill(pid, 15); time.sleep(1.5)
print("state files:", sorted(os.listdir(st)))
print(open(os.path.join(st, "mtk-wmt-init.log")).read()[-2500:])
