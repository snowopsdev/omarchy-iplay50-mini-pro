#!/usr/bin/env python3
"""Write the quiet speaker test tone used by test-speaker.sh.

48000 Hz, S16_LE, stereo, 440 Hz sine at -30 dBFS: 1.5 s left only,
1.5 s right only, then 1.5 s both channels (4.5 s total), with 100 ms
fades at each segment edge to avoid clicks.
Usage: python3 -I make-tone.py [output.wav]
"""
import math
import struct
import sys
import wave

RATE, FREQ, SECONDS = 48000, 440.0, 1.5
AMP = round(32767 * 10 ** (-30 / 20))
out = sys.argv[1] if len(sys.argv) > 1 else 'tone-440-m30dBFS-L-R-LR.wav'
seg = int(RATE * SECONDS)
fade = RATE // 10
frames = bytearray()
for left, right in ((1, 0), (0, 1), (1, 1)):
    for n in range(seg):
        gain = min(1.0, n / fade, (seg - 1 - n) / fade)
        s = round(AMP * gain * math.sin(2 * math.pi * FREQ * n / RATE))
        frames += struct.pack('<hh', s * left, s * right)
with wave.open(out, 'wb') as w:
    w.setnchannels(2)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(bytes(frames))
print(out)
