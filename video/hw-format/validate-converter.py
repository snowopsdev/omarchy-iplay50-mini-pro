#!/usr/bin/env python3
"""Validate the C ABI using an actual hardware frame and rejection cases."""
import argparse
import ctypes
import errno
import hashlib
import json
import platform
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--library", type=Path, required=True)
p.add_argument("--plane", type=Path,
               default=Path(__file__).resolve().parent.parent /
               "evidence/m21s-hw2.plane0.bin")
p.add_argument("--reference", type=Path,
               default=Path(__file__).resolve().parent.parent /
               "hw-driver/sample/first-frame.yuv420p")
a = p.parse_args()
lib = ctypes.CDLL(str(a.library.resolve()))
convert = lib.tablet_m21s_to_nv12
convert.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32,
                    ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p,
                    ctypes.c_size_t, ctypes.c_uint32]
convert.restype = ctypes.c_int
raw = a.plane.read_bytes()
reference = a.reference.read_bytes()
if len(raw) != 122880 or len(reference) != 115200:
    p.error("validation fixture must be captured 320x256 M21S / visible320x240 I420")
src = ctypes.create_string_buffer(raw)
cases = {}
for stride in (320, 384):
    need = stride * 256 * 3 // 2
    guard = ctypes.create_string_buffer(bytes([0xA5]) * (need + 32), need + 32)
    dst = ctypes.byref(guard, 16)
    assert convert(src, len(raw), 320, 256, 320, dst, need, stride) == 0
    output = guard.raw[16:16 + need]
    assert guard.raw[:16] == bytes([0xA5]) * 16
    assert guard.raw[-16:] == bytes([0xA5]) * 16
    y = b"".join(output[i * stride:i * stride + 320] for i in range(240))
    uv_start = stride * 256
    uv = b"".join(output[uv_start + i * stride:uv_start + i * stride + 320]
                  for i in range(120))
    i420 = y + uv[0::2] + uv[1::2]
    assert i420 == reference
    cases[f"hardware_frame_dst_stride_{stride}"] = {
        "pixels_exact": True, "visible_i420_sha256": hashlib.sha256(i420).hexdigest(),
        "guards_intact": True,
    }

dst = ctypes.create_string_buffer(122880)
normal = [src, len(raw), 320, 256, 320, dst, len(dst), 320]
bad = [
    ("null_source", 0, None, -errno.EINVAL),
    ("short_source", 1, len(raw) - 1, -errno.ENOSPC),
    ("zero_width", 2, 0, -errno.EINVAL),
    ("odd_width", 2, 319, -errno.EINVAL),
    ("oversize_width", 2, 8194, -errno.EINVAL),
    ("non_tile_height", 3, 240, -errno.EINVAL),
    ("short_source_stride", 4, 304, -errno.EINVAL),
    ("non_tile_source_stride", 4, 328, -errno.EINVAL),
    ("null_destination", 5, None, -errno.EINVAL),
    ("short_destination", 6, len(dst) - 1, -errno.ENOSPC),
    ("short_destination_stride", 7, 318, -errno.EINVAL),
    ("odd_destination_stride", 7, 321, -errno.EINVAL),
    ("overlapping_ranges", 5, src, -errno.EINVAL),
]
for name, index, value, expected in bad:
    args = normal.copy()
    args[index] = value
    before = dst.raw
    actual = convert(*args)
    assert actual == expected, (name, actual, expected)
    assert dst.raw == before
    cases[name] = {"return": actual, "destination_unchanged": True}

print(json.dumps({
    "machine": platform.machine(),
    "library_sha256": hashlib.sha256(a.library.read_bytes()).hexdigest(),
    "hardware_plane_sha256": hashlib.sha256(raw).hexdigest(),
    "software_reference_sha256": hashlib.sha256(reference).hexdigest(),
    "cases": cases, "all_passed": True,
}, indent=2))
