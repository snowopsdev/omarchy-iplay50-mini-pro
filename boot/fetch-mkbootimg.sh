#!/bin/bash
# Fetch AOSP mkbootimg/unpack_bootimg at the commit pinned by the GKI build manifest (tools/mkbootimg 34bc8bfb)
# into <repo>/work/mkbootimg. Third-party (Apache-2.0), not shipped in this repo.
# sha256 is of the gitiles archive as downloaded 2026-10-27; gitiles generates archives on the fly, so a mismatch
# can be harmless - then compare mkbootimg.py against the file at that commit instead.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
W=$REPO/work/mkbootimg
REV=34bc8bfb493401f7aadbd3b434f9fbfa521e9301
mkdir -p "$W"
curl -fL -o "$W.tar.gz" "https://android.googlesource.com/platform/system/tools/mkbootimg/+archive/$REV.tar.gz"
echo "5235cc83ab46ccef2d8ff7914e590bcf627b978465771b518548e2b325821f73  $W.tar.gz" | sha256sum -c - \
	|| echo "WARNING: archive hash differs from the one recorded (see comment above)" >&2
tar -xzf "$W.tar.gz" -C "$W"
ls "$W"/mkbootimg.py "$W"/unpack_bootimg.py
