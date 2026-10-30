#!/usr/bin/env python3
"""Compare a captured first frame against the independent software reference.

Consumes native probe plane dumps only; no device access. MT21 compressed output
is deliberately unsupported: naming it NV12 does not decompress it.
"""
import argparse
import ctypes
import ctypes.util
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("prefix", help="probe dump prefix, without .planeN.bin")
p.add_argument("--format", required=True, choices=["NV12", "NM12", "MM21", "YU12", "YM12"])
p.add_argument("--coded-width", type=int, required=True)
p.add_argument("--coded-height", type=int, required=True)
p.add_argument("--stride-y", type=int, required=True)
p.add_argument("--stride-uv", type=int, required=True)
p.add_argument("--width", type=int, default=320)
p.add_argument("--height", type=int, default=240)
p.add_argument("--reference", type=Path,
               default=Path(__file__).resolve().parent / "sample/first-frame.yuv420p")
a = p.parse_args()
assert 0 < a.width <= a.coded_width <= 8192 and 0 < a.height <= a.coded_height <= 8192
assert not any(x % 2 for x in [a.width, a.height, a.coded_width, a.coded_height])
assert a.stride_y >= a.coded_width

def read(index):
    return Path(f"{a.prefix}.plane{index}.bin").read_bytes()

def rows(data, stride, width, height):
    assert len(data) >= stride * (height - 1) + width
    return b"".join(data[y * stride:y * stride + width] for y in range(height))

y_data = read(0)
if a.format in ["NM12", "MM21"]:
    uv_data = read(1)
elif a.format == "NV12":
    split = a.stride_y * a.coded_height
    y_data, uv_data = y_data[:split], y_data[split:]
else:
    if a.format == "YM12":
        u_data, v_data = read(1), read(2)
    else:
        split = a.stride_y * a.coded_height
        chroma = a.stride_uv * a.coded_height // 2
        y_data, u_data, v_data = y_data[:split], y_data[split:split + chroma], y_data[split + chroma:]

if a.format == "MM21":
    # Use the same upstream conversion function as Chromium, not a guessed tile map.
    assert len(y_data) >= a.stride_y * ((a.coded_height + 31) // 32 * 32)
    assert len(uv_data) >= a.stride_uv * ((a.coded_height // 2 + 15) // 16 * 16)
    lib = ctypes.CDLL(ctypes.util.find_library("yuv") or "libyuv.so")
    func = lib.MM21ToNV12
    func.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int,
                     ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int,
                     ctypes.c_int, ctypes.c_int]
    func.restype = ctypes.c_int
    src_y = ctypes.create_string_buffer(y_data)
    src_uv = ctypes.create_string_buffer(uv_data)
    dst_y = ctypes.create_string_buffer(a.coded_width * a.coded_height)
    dst_uv = ctypes.create_string_buffer(a.coded_width * a.coded_height // 2)
    assert func(src_y, a.stride_y, src_uv, a.stride_uv, dst_y, a.coded_width,
                dst_uv, a.coded_width, a.coded_width, a.coded_height) == 0
    y_data, uv_data = dst_y.raw, dst_uv.raw
    a.stride_y = a.stride_uv = a.coded_width

y = rows(y_data, a.stride_y, a.width, a.height)
if a.format in ["NV12", "NM12", "MM21"]:
    uv = rows(uv_data, a.stride_uv, a.width, a.height // 2)
    u, v = uv[0::2], uv[1::2]
else:
    u = rows(u_data, a.stride_uv, a.width // 2, a.height // 2)
    v = rows(v_data, a.stride_uv, a.width // 2, a.height // 2)
actual = y + u + v
reference = a.reference.read_bytes()
assert len(reference) == len(actual)
delta = [abs(x - y) for x, y in zip(actual, reference)]
result = {
    "format": a.format, "visible_size": [a.width, a.height],
    "actual_sha256": hashlib.sha256(actual).hexdigest(),
    "reference_sha256": hashlib.sha256(reference).hexdigest(),
    "pixels_exact": actual == reference, "max_sample_error": max(delta),
    "mean_sample_error": sum(delta) / len(delta),
    "samples_different": sum(d != 0 for d in delta),
}
result["within_tolerance"] = result["max_sample_error"] <= 2 and result["mean_sample_error"] <= 0.1
print(json.dumps(result, indent=2))
raise SystemExit(0 if result["within_tolerance"] else 1)
