#!/bin/bash
# Download the generic Arch Linux ARM aarch64 root tarball and verify it (md5 + GPG signature of the
# "Arch Linux ARM Build System" key 68B3537F39A313B3E574D06777193F152BDBE6A6), into <repo>/work/rootfs/dl.
# Commands as recorded in the project's poc/NOTES.md (2026-10-27). "latest" moves: you will get a newer tarball.
# If --recv-keys fails on your default keyserver, add e.g. --keyserver hkps://keyserver.ubuntu.com (not what the notes used).
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
W=$REPO/work/rootfs/dl; mkdir -p "$W"; cd "$W"
T=ArchLinuxARM-aarch64-latest.tar.gz
for f in $T $T.md5 $T.sig; do [ -s "$f" ] || curl -fLO "http://os.archlinuxarm.org/os/$f"; done
[ "$(md5sum < $T | cut -d' ' -f1)" = "$(cut -d' ' -f1 $T.md5)" ] && echo "md5 OK" || { echo "md5 MISMATCH" >&2; exit 1; }
export GNUPGHOME=$W/gnupg; mkdir -p -m 700 "$GNUPGHOME"
gpg --recv-keys 68B3537F39A313B3E574D06777193F152BDBE6A6
gpg --verify $T.sig $T
