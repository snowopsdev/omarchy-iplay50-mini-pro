#!/usr/bin/env python3
"""Build the initramfs pieces without root (we write the newc cpios ourselves).
Kernel-embedded (stage0 + fallback): everything we own lives under /omarchy; the only other entries are /linuxinit (rdinit =
stage0) and /dev/console,/dev/null. Android's vendor/generic ramdisks are unpacked ON TOP and own /bin /etc /sbin /init /system /lib.
Outputs (in $OUT, default work/initramfs/out): initramfs-builtin.cpio (uncompressed newc, CONFIG_INITRAMFS_SOURCE), initramfs.cpio.gz (+ -builtin.cpio.gz copy),
stage1.cpio (raw uncompressed newc for init_boot_b, <= 8 MiB, extracted by stage0 to /stage1, runs /stage1/init),
stage1-diag.cpio (minimal diagnostic stage1). Env: NO_AUTOREBOOT=1 bakes /omarchy/etc/no-autoreboot; STAGE1_TRACE=0 = no xtrace."""
import gzip, os, stat, subprocess
root = os.path.dirname(os.path.abspath(__file__))                       # <repo>/initramfs
WORK = os.environ.get("WORK", f"{os.path.dirname(root)}/work/initramfs")  # downloads + outputs (not in git)
SRC = f"{root}/initramfs-src"
EXT = os.environ.get("ALPINE_EXT", f"{WORK}/alpine/ext")                # from initramfs/fetch-alpine.sh
BBS = f"{EXT}/busybox-static-1.37.0-r31/bin/busybox.static"
OUT = os.environ.get("OUT", f"{WORK}/out")
RBH = os.environ.get("REBOOT_HELPER", f"{WORK}/reboot-bootloader")     # build-reboot-bootloader.sh; tests: a stub (the real one would really reboot!)
MTIME = 1700000000
ents = []
def d(name, mode=0o755): ents.append((name, stat.S_IFDIR | mode, b"", (0, 0)))
def f(name, path, mode=0o755): ents.append((name, stat.S_IFREG | mode, open(path, "rb").read(), (0, 0)))
def fd(name, data, mode=0o644): ents.append((name, stat.S_IFREG | mode, data, (0, 0)))
def l(name, target): ents.append((name, stat.S_IFLNK | 0o777, target.encode(), (0, 0)))
def c(name, mode, maj, mn): ents.append((name, stat.S_IFCHR | mode, b"", (maj, mn)))

applets = subprocess.run(["qemu-aarch64", BBS, "--list"], capture_output=True, text=True, check=True).stdout.split()
own = {"busybox", "busybox-extras", "usb-gadget", "rescue-shell", "continue-boot", "reboot-bootloader"}
BBE = f"{EXT}/busybox-extras-1.37.0-r31/bin/busybox-extras"
MUSL = f"{EXT}/musl-1.2.6-r3/lib/ld-musl-aarch64.so.1"

d("dev"); c("dev/console", 0o600, 5, 1); c("dev/null", 0o666, 1, 3)
f("linuxinit", f"{SRC}/linuxinit")                      # stage0
for p in ["omarchy/libexec"]: pass
for p in ["omarchy", "omarchy/bin", "omarchy/sbin", "omarchy/lib", "omarchy/etc", "omarchy/libexec"]: d(p)
f("omarchy/libexec/init-main", f"{SRC}/init-main")        # built-in full init (also the stage1 payload)
f("omarchy/bin/busybox", BBS)
for a in sorted(applets):
    if a not in own: l(f"omarchy/bin/{a}", "busybox")
f("omarchy/bin/busybox-extras", f"{EXT}/busybox-extras-1.37.0-r31/bin/busybox-extras")
f("omarchy/bin/reboot-bootloader", RBH)
for s in ["usb-gadget", "rescue-shell", "continue-boot"]: f(f"omarchy/bin/{s}", f"{SRC}/bin/{s}")
f("omarchy/lib/ld-musl-aarch64.so.1", f"{EXT}/musl-1.2.6-r3/lib/ld-musl-aarch64.so.1")
l("omarchy/lib/libc.musl-aarch64.so.1", "ld-musl-aarch64.so.1")
fd("omarchy/etc/VERSION", b"omarchy-tablet initramfs; busybox 1.37.0-r31 (Alpine), musl 1.2.6-r3\n")
if os.environ.get("NO_AUTOREBOOT") == "1": fd("omarchy/etc/no-autoreboot", b"")


def pad4(n): return (4 - n % 4) % 4
def newc(ents):
    out = bytearray()
    for ino, (name, mode, data, (maj, mn)) in enumerate(ents, 1):
        nm = name.encode() + b"\0"
        hdr = "070701" + "".join("%08X" % v for v in (ino, mode, 0, 0, 2 if stat.S_ISDIR(mode) else 1, MTIME, len(data), 0, 0, maj, mn, len(nm), 0))
        out += hdr.encode() + nm + b"\0" * pad4(110 + len(nm)) + data + b"\0" * pad4(len(data))
    nm = b"TRAILER!!!\0"
    out += ("070701" + "".join("%08X" % v for v in (0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, len(nm), 0))).encode() + nm + b"\0" * pad4(110 + len(nm))
    out += b"\0" * ((-len(out)) % 512)
    return bytes(out)

os.makedirs(OUT, exist_ok=True)
main = newc(ents)
open(f"{OUT}/initramfs-builtin.cpio", "wb").write(main)
gz = gzip.compress(main, compresslevel=9, mtime=0)
for n in ("initramfs.cpio.gz", "initramfs-builtin.cpio.gz"): open(f"{OUT}/{n}", "wb").write(gz)
print("builtin: entries", len(ents), "cpio", len(main), "gz", len(gz))

# ---- stage1 (init_boot_b payload): self-contained scripts/helpers; busybox + applets come from the builtin /omarchy
ents.clear()
for p in ["bin", "lib", "etc"]: d(p)
f("init", f"{SRC}/init-main")
for s_ in ["usb-gadget", "rescue-shell", "continue-boot"]: f(f"bin/{s_}", f"{SRC}/bin/{s_}")
f("bin/reboot-bootloader", RBH)
f("bin/busybox-extras", BBE)
f("lib/ld-musl-aarch64.so.1", MUSL); l("lib/libc.musl-aarch64.so.1", "ld-musl-aarch64.so.1")
fd("etc/VERSION", b"omarchy-tablet stage1\n")
if os.environ.get("STAGE1_TRACE", "1") == "1": fd("etc/verbose", b"")
s1 = newc(ents)
assert len(s1) <= 8 * 1024 * 1024, "stage1 does not fit init_boot_b"
open(f"{OUT}/stage1.cpio", "wb").write(s1)
print("stage1.cpio", len(s1), "bytes (limit 8388608)")
fd("etc/force-rescue", b"")   # same payload, but stays in the initramfs rescue shell (root still mounted at /newroot)
open(f"{OUT}/stage1-rescue.cpio", "wb").write(newc(ents))
print("stage1-rescue.cpio written")

# ---- stage1-diag
ents.clear()
d("bin"); d("etc")
f("init", f"{SRC}/diag-init")
f("bin/reboot-bootloader", RBH)
fd("etc/VERSION", b"omarchy-tablet stage1-diag\n")
dg = newc(ents)
open(f"{OUT}/stage1-diag.cpio", "wb").write(dg)
print("stage1-diag.cpio", len(dg), "bytes")
