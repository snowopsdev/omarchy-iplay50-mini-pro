#!/usr/bin/env bash
# Fetch upstream wvkbd v0.20, apply this repo's tablet patch, and build wvkbd-mobintl.
#
#   touch-ui/wvkbd/build.sh prepare   # clone + verify + patch into touch-ui/build/wvkbd (desktop or tablet)
#   touch-ui/wvkbd/build.sh host      # prepare, then build a host binary (only for touch-ui/tests/run.sh)
#   touch-ui/wvkbd/build.sh tablet    # prepare, copy the tree to the tablet, build there as user hypr,
#                                     # and copy the arm64 binary back to touch-ui/out/wvkbd-mobintl
#
# Upstream: https://github.com/jjsullivan5196/wvkbd tag v0.20,
#           commit 6b41504a0cb58fd1163fa44692398fbd61f8905f (GPL v3.0 per upstream COPYING; the patch is a derivative under the same terms).
# Build deps (Arch / Arch Linux ARM): gcc make pkgconf wayland wayland-protocols libxkbcommon cairo pango
# The explicit wvkbd-mobintl target skips the optional scdoc man page.
# Public-repo note: the "tablet" mode mirrors the steps recorded in the notes (copy the tree to
# /home/hypr/src, make as hypr, copy the binary back); this script itself was not run against the device.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
URL=https://github.com/jjsullivan5196/wvkbd
TAG=v0.20
COMMIT=6b41504a0cb58fd1163fa44692398fbd61f8905f
PATCH=$REPO/touch-ui/wvkbd/tablet-wvkbd-v0.20.patch
SRC=${WVKBD_SRC:-$REPO/touch-ui/build/wvkbd}

prepare() {
  if [[ ! -d $SRC/.git ]]; then
    mkdir -p "$(dirname "$SRC")"
    git clone --quiet --branch "$TAG" --depth 1 "$URL" "$SRC"
  fi
  local head
  head=$(git -C "$SRC" rev-parse HEAD)
  [[ $head == "$COMMIT" ]] || { echo "unexpected upstream commit $head (want $COMMIT)" >&2; exit 1; }
  if [[ -f $SRC/tablet-layouts.h ]]; then
    echo "patch already applied in $SRC"
  else
    (cd "$SRC" && patch -p1 --quiet < "$PATCH")
    echo "patched $SRC"
  fi
}

case ${1:-prepare} in
  prepare) prepare ;;
  host)
    prepare
    make -C "$SRC" LAYOUT=mobintl -j"$(nproc)" wvkbd-mobintl
    ;;
  tablet)
    prepare
    T=$REPO/tools/tablet
    # Copy sources only (no host objects/binaries), build as hypr, fetch the binary.
    tar -C "$SRC" --exclude=.git --exclude='build-*' --exclude='*.o' --exclude=wvkbd-mobintl -cf - . |
      "$T" 'rm -rf /home/hypr/src/wvkbd-tablet && install -d -o hypr -g hypr /home/hypr/src/wvkbd-tablet &&
            runuser -u hypr -- tar -C /home/hypr/src/wvkbd-tablet -xf - &&
            cd /home/hypr/src/wvkbd-tablet &&
            runuser -u hypr -- make LAYOUT=mobintl clean >/dev/null &&
            runuser -u hypr -- make LAYOUT=mobintl -j4 wvkbd-mobintl >&2'
    mkdir -p "$REPO/touch-ui/out"
    "$T" 'cat /home/hypr/src/wvkbd-tablet/wvkbd-mobintl' > "$REPO/touch-ui/out/wvkbd-mobintl"
    chmod 755 "$REPO/touch-ui/out/wvkbd-mobintl"
    sha256sum "$REPO/touch-ui/out/wvkbd-mobintl"
    ;;
  *) echo "usage: $0 prepare|host|tablet" >&2; exit 2 ;;
esac
