#!/usr/bin/env python3
"""Test an explicit contiguous-MM21 candidate against a software-decoded frame.

Reads saved bytes only. M21S's vendor description establishes uncompressed
contiguous block planes, but does not prove the MM21 tile map or UV offset.
A successful pixel comparison is required before accepting that interpretation.
"""
import argparse
import ctypes
import ctypes.util
import hashlib
import json
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("plane", type=Path, help="saved contiguous CAPTURE plane0")
    p.add_argument("--candidate-contiguous-mm21", action="store_true", required=True)
    p.add_argument("--coded-width", type=int, required=True)
    p.add_argument("--coded-height", type=int, required=True)
    p.add_argument("--stride", type=int, required=True)
    p.add_argument("--width", type=int, default=320)
    p.add_argument("--height", type=int, default=240)
    p.add_argument("--reference", type=Path,
                   default=Path(__file__).resolve().parent.parent /
                   "hw-driver/sample/first-frame.yuv420p")
    p.add_argument("--write-coded-nv12", type=Path,
                   help="optional detiled candidate, still requires comparison")
    a = p.parse_args()
    if not (0 < a.width <= a.coded_width <= 8192 and
            0 < a.height <= a.coded_height <= 8192):
        p.error("visible and coded dimensions must be positive and at most 8192")
    if (any(v % 2 for v in [a.width, a.height, a.coded_width, a.coded_height]) or
            a.coded_height % 32 or a.stride % 16 or
            not a.coded_width <= a.stride <= 16384):
        p.error("candidate needs even dimensions, coded height multiple of 32, "
                "and 16-byte-aligned stride >= coded width")

    source = a.plane.read_bytes()
    reference = a.reference.read_bytes()
    uv_offset = a.stride * a.coded_height
    minimum_bytes = uv_offset + a.stride * (a.coded_height // 2)
    if len(source) < minimum_bytes:
        p.error(f"plane too short: {len(source)} bytes, candidate requires {minimum_bytes}")
    if len(reference) != a.width * a.height * 3 // 2:
        p.error("reference must contain exactly one visible I420 frame")

    lib_name = ctypes.util.find_library("yuv") or "libyuv.so"
    lib = ctypes.CDLL(lib_name)
    convert = lib.MM21ToNV12
    convert.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int,
                        ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int,
                        ctypes.c_int, ctypes.c_int]
    convert.restype = ctypes.c_int
    src = ctypes.create_string_buffer(source[:minimum_bytes])
    dst_y = ctypes.create_string_buffer(a.coded_width * a.coded_height)
    dst_uv = ctypes.create_string_buffer(a.coded_width * a.coded_height // 2)
    ret = convert(src, a.stride, ctypes.byref(src, uv_offset), a.stride,
                  dst_y, a.coded_width, dst_uv, a.coded_width,
                  a.coded_width, a.coded_height)
    if ret != 0:
        raise RuntimeError(f"MM21ToNV12 failed: {ret}")

    def crop(data, rows):
        return b"".join(data[i * a.coded_width:i * a.coded_width + a.width]
                        for i in range(rows))

    y = crop(dst_y.raw, a.height)
    uv = crop(dst_uv.raw, a.height // 2)
    actual_planes = [y, uv[0::2], uv[1::2]]
    actual = b"".join(actual_planes)
    deltas = [abs(x - y) for x, y in zip(actual, reference)]
    result = {
        "reported_format": "M21S",
        "layout_under_test": "contiguous-MM21 candidate; not inferred from FourCC alone",
        "coded_size": [a.coded_width, a.coded_height],
        "visible_size": [a.width, a.height],
        "source_stride": a.stride,
        "candidate_uv_offset": uv_offset,
        "source_bytes": len(source),
        "candidate_required_bytes": minimum_bytes,
        "source_sha256": hashlib.sha256(source).hexdigest(),
        "actual_sha256": hashlib.sha256(actual).hexdigest(),
        "reference_sha256": hashlib.sha256(reference).hexdigest(),
        "libyuv": lib_name,
        "pixels_exact": actual == reference,
        "max_sample_error": max(deltas),
        "mean_sample_error": sum(deltas) / len(deltas),
        "samples_different": sum(d != 0 for d in deltas),
        "within_tolerance": max(deltas) <= 2 and sum(deltas) / len(deltas) <= 0.1,
    }
    offset = 0
    result["planes"] = {}
    for name, plane in zip(["Y", "U", "V"], actual_planes):
        expected = reference[offset:offset + len(plane)]
        delta = [abs(x - y) for x, y in zip(plane, expected)]
        result["planes"][name] = {
            "max_sample_error": max(delta), "mean_sample_error": sum(delta) / len(delta)
        }
        offset += len(plane)
    if a.write_coded_nv12:
        a.write_coded_nv12.write_bytes(dst_y.raw + dst_uv.raw)
    print(json.dumps(result, indent=2))
    return 0 if result["within_tolerance"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
