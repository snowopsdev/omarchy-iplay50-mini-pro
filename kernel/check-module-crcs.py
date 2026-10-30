#!/usr/bin/env python3
"""check-module-crcs.py <vmlinux.symvers> <module.ko|dir>...

For each vendor module, compare every CONFIG_MODVERSIONS import CRC (__versions section) that
refers to a vmlinux symbol with the CRC in <vmlinux.symvers> of a kernel you built, and print
the module's vermagic. Imports that are not in vmlinux.symvers are provided by other modules
and are only counted. Exit status 1 if any vmlinux CRC differs or any vmlinux import is missing.

Typical use (on the desktop, after copying the device's modules somewhere):
    check-module-crcs.py out/vmlinux.symvers vendor_dlkm/lib/modules vendor_ramdisk/lib/modules
"""
import os
import struct
import sys


def sections(data):
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        raise ValueError("not a little-endian ELF64 file")
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", data, 0x3A)
    hdrs = [struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * shentsize) for i in range(shnum)]
    stroff = hdrs[shstrndx][4]
    out = {}
    for h in hdrs:
        name = data[stroff + h[0]:data.index(b"\0", stroff + h[0])].decode()
        out[name] = data[h[4]:h[4] + h[5]]
    return out


def versions(sec):
    for i in range(0, len(sec), 64):
        crc, = struct.unpack_from("<Q", sec, i)
        name = sec[i + 8:i + 64].split(b"\0", 1)[0].decode()
        yield name, crc & 0xFFFFFFFF


def vermagic(modinfo):
    for kv in modinfo.split(b"\0"):
        if kv.startswith(b"vermagic="):
            return kv[9:].decode()
    return "?"


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    symvers = {}
    for line in open(sys.argv[1]):
        f = line.rstrip("\n").split("\t")
        symvers[f[1]] = int(f[0], 16)
    mods = []
    for p in sys.argv[2:]:
        if os.path.isdir(p):
            mods += sorted(os.path.join(r, n) for r, _, ns in os.walk(p) for n in ns if n.endswith(".ko"))
        else:
            mods.append(p)
    bad = 0
    magics = set()
    for m in mods:
        s = sections(open(m, "rb").read())
        mg = vermagic(s.get(".modinfo", b""))
        magics.add(mg)
        ok = ext = 0
        errs = []
        for name, crc in versions(s.get("__versions", b"")):
            if name not in symvers:
                ext += 1
            elif symvers[name] == crc:
                ok += 1
            else:
                errs.append(f"{name} module=0x{crc:08x} kernel=0x{symvers[name]:08x}")
        status = "OK " if not errs else "BAD"
        print(f"{status} {os.path.basename(m)}: {ok} vmlinux CRCs match, {len(errs)} differ, {ext} from other modules")
        for e in errs[:20]:
            print("     ", e)
        bad += bool(errs)
    print(f"modules: {len(mods)}, with CRC mismatches: {bad}")
    print("vermagic seen:", "; ".join(sorted(magics)))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
