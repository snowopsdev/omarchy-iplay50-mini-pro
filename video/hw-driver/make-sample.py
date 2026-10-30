#!/usr/bin/env python3
"""Create a small, deterministic, noncopyrighted H.264 decode fixture."""
from pathlib import Path
import hashlib
import json
import re
import struct
import subprocess

out = Path(__file__).resolve().parent / "sample"
out.mkdir(exist_ok=True)
h264 = out / "testsrc-320x240.h264"
cmd = [
    "ffmpeg", "-v", "error", "-nostdin", "-y", "-f", "lavfi", "-i",
    "testsrc2=size=320x240:rate=10", "-frames:v", "12", "-an", "-c:v", "libx264",
    "-profile:v", "baseline", "-pix_fmt", "yuv420p", "-preset", "medium", "-qp", "20",
    "-g", "1", "-bf", "0", "-threads", "1", "-x264-params",
    "aud=1:repeat-headers=1:scenecut=0", "-f", "h264", str(h264),
]
subprocess.run(cmd, check=True)
data = h264.read_bytes()
aud = [m.start() for m in re.finditer(rb"\x00\x00(?:\x00)?\x01\x09", data)]
assert len(aud) == 12 and aud[0] == 0, aud
ends = aud[1:] + [len(data)]
units = [data[start:end] for start, end in zip(aud, ends)]
framed = out / "testsrc-320x240.au"
framed.write_bytes(b"".join(struct.pack("<I", len(unit)) + unit for unit in units))
raw = out / "first-frame.yuv420p"
subprocess.run([
    "ffmpeg", "-v", "error", "-nostdin", "-y", "-i", str(h264), "-frames:v", "1",
    "-pix_fmt", "yuv420p", "-f", "rawvideo", str(raw),
], check=True)
assert raw.stat().st_size == 320 * 240 * 3 // 2
meta = {
    "source": "FFmpeg testsrc2, generated locally; no downloaded content",
    "width": 320, "height": 240, "frames": 12, "fps": 10,
    "access_unit_bytes": [len(unit) for unit in units], "ffmpeg_command": cmd,
    "ffmpeg_version": subprocess.check_output(["ffmpeg", "-version"], text=True).splitlines()[0],
    "sha256": {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
               for path in [h264, framed, raw]},
}
(out / "metadata.json").write_text(json.dumps(meta, indent=2) + "\n")
print(json.dumps(meta, indent=2))
