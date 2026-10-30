#!/usr/bin/env python3
"""Stage/verify a private DMABUF-only decoder module. Never installs or loads.

Only two source-backed MOVK immediates change, removing VB2_MMAP from io_modes.
Exact retained stock SHA256 is mandatory; no symbols, ABI, vermagic, relocation,
partition or force-loading changes. Output creation is exclusive.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct

STOCK_SHA256 = "0a5d904c798a35297e4afd9af76f21556f10544bcee0fb4c9f416429723227b7"
LOCATIONS = [0x10218, 0x10434]
OLD = bytes.fromhex("2802c0f2")  # movk x8,#0x11,lsl#32
NEW = bytes.fromhex("0802c0f2")  # movk x8,#0x10,lsl#32

def digest(data):
    return hashlib.sha256(data).hexdigest()

def text_offset(data):
    assert data[:6] == b"\x7fELF\x02\x01", "requires ELF64 little-endian"
    assert struct.unpack_from("<H", data, 18)[0] == 183, "requires AArch64"
    shoff = struct.unpack_from("<Q", data, 40)[0]
    entsize, count, strings_index = struct.unpack_from("<HHH", data, 58)
    assert entsize == 64 and count and strings_index < count
    assert shoff + entsize * count <= len(data)
    def section(i):
        return struct.unpack_from("<IIQQQQIIQQ", data, shoff + i * entsize)
    names = section(strings_index)
    strings = data[names[4]:names[4] + names[5]]
    for i in range(count):
        s = section(i)
        end = strings.find(b"\0", s[0])
        if strings[s[0]:end] == b".text":
            assert s[1] == 1 and s[2] & 4 and s[3] == 0
            assert s[4] + s[5] <= len(data) and s[5] > max(LOCATIONS) + 4
            return s[4]
    raise AssertionError("missing .text")

def expected(stock):
    assert digest(stock) == STOCK_SHA256, "unexpected retained stock module hash"
    off = text_offset(stock)
    modified = bytearray(stock)
    edits = []
    for address in LOCATIONS:
        pos = off + address
        assert stock[pos:pos+4] == OLD, "instruction does not match pinned MOVK"
        modified[pos:pos+4] = NEW
        edits.append({"text_address": hex(address), "file_offset": hex(pos),
                      "before": OLD.hex(), "after": NEW.hex()})
    changes = [i for i, (a,b) in enumerate(zip(stock, modified)) if a != b]
    assert changes == [off + x for x in LOCATIONS], "unexpected byte changes"
    return bytes(modified), edits

p = argparse.ArgumentParser(description=__doc__)
sub = p.add_subparsers(dest="command", required=True)
s = sub.add_parser("stage")
s.add_argument("stock", type=Path)
s.add_argument("output", type=Path)
v = sub.add_parser("verify")
v.add_argument("stock", type=Path)
v.add_argument("artifact", type=Path)
a = p.parse_args()
stock = a.stock.read_bytes()
modified, edits = expected(stock)
metadata = {"stock_sha256": digest(stock), "guarded_sha256": digest(modified),
            "module_bytes": len(stock), "edits": edits,
            "effect": "OUTPUT and CAPTURE io_modes: VB2_DMABUF only (0x10); MMAP bit removed",
            "not_changed": ["vermagic", "module versions", "symbol ABI", "relocations"],
            "runtime_verified": False}
if a.command == "stage":
    sidecar = Path(str(a.output) + ".json")
    assert not a.output.exists() and not sidecar.exists(), "refusing overwrite"
    fd = os.open(a.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(modified)
        f.flush()
        os.fsync(f.fileno())
    fd = os.open(sidecar, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w") as f:
        json.dump(metadata, f, indent=2)
        f.write("\n")
else:
    actual = a.artifact.read_bytes()
    assert actual == modified, "artifact is not the exact expected two-instruction guard"
    metadata["artifact_verified"] = str(a.artifact)
print(json.dumps(metadata, indent=2))
