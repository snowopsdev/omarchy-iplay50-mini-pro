#!/bin/bash
# Download + verify the Alpine aarch64 packages the initramfs is built from, and extract them to
# <repo>/work/initramfs/alpine/ext/<pkg>/ (what build-initramfs.py expects).
#   busybox-static : static busybox for /omarchy/bin (all applets are symlinks to it)
#   busybox-extras : telnetd for the rescue shell (Alpine's busybox-static has NO telnetd); dynamic, musl
#   musl           : ld-musl-aarch64.so.1 for busybox-extras (run through the loader, so Android's /lib is untouched)
#   alpine-keys    : the signing key used to check the other packages
# Checks: pinned sha256 (as downloaded 2026-10-27) + RSA-SHA1 signature over the control stream + datahash.
# These exact revisions were in Alpine v3.24 (= latest-stable on 2026-10-30). If Alpine moves on, set ALPINE_BASE to a
# mirror/archive that still has them; changing versions means editing build-initramfs.py and this file.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
W=${WORK:-$REPO/work/initramfs}/alpine
ALPINE_BASE=${ALPINE_BASE:-https://dl-cdn.alpinelinux.org/alpine/v3.24/main/aarch64}
SUMS="883cfeda387f655cfc2c466995036b82abd77f8f78e1b23dc70e5154bcf9cc0b  alpine-keys-2.6-r0.apk
64a2c66f44f2218473f56dd63355d988c3da0bdae6384d6d94d32587328ccea3  busybox-extras-1.37.0-r31.apk
965777e06b94bf11981d5f4ecdfcd577879f4b0dda544294d1dd3f72b217bc75  busybox-static-1.37.0-r31.apk
2e17864102278d9049c3a56f1bceed9e8cf287065617e579325123443daed8a5  musl-1.2.6-r3.apk"
mkdir -p "$W/ext"
cd "$W"
for f in $(echo "$SUMS" | awk '{print $2}'); do
	[ -s "$f" ] || curl -fL -o "$f" "$ALPINE_BASE/$f"
done
echo "$SUMS" | sha256sum -c -
# key: unpack alpine-keys (an .apk is concatenated gzip'd tars; -i reads past the first end-of-archive)
mkdir -p keys
tar --warning=no-unknown-keyword -xzif alpine-keys-2.6-r0.apk -C keys
KEY=keys/usr/share/apk/keys/aarch64/alpine-devel@lists.alpinelinux.org-616ae350.rsa.pub
for p in alpine-keys-2.6-r0 busybox-static-1.37.0-r31 busybox-extras-1.37.0-r31 musl-1.2.6-r3; do
	python3 -I "$HERE/tools/apkfetch.py" "$p.apk" "$KEY" "ext/$p"
done
ls -l ext/busybox-static-1.37.0-r31/bin/busybox.static ext/busybox-extras-1.37.0-r31/bin/busybox-extras \
	ext/musl-1.2.6-r3/lib/ld-musl-aarch64.so.1
