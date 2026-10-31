#!/usr/bin/env bash
# Reconstruct the exact PanVK/kbase ("FourFect") source tree used by the
# tablet compositor, from pristine upstream pieces plus this repo's patch.
#
#   Mesa base     https://gitlab.freedesktop.org/mesa/mesa.git
#                 commit feeadb5f55f4ba14a5c5ddc84ba20177e419cffb
#   kbase/G57 set https://github.com/Noysz/panvk-g99-jm
#                 commit 08aa2e62580e447b28fe419d8995b84d8d6f1fff
#                 patches-upstream/0001..0005, applied in order
#   This repo     patches/fourfect-kbase-only-build.patch
#                 patches/panvk-kbase-signalled-export.patch, only with
#                 PANVK_APPS=1 (the build for applications and Chromium;
#                 the compositor keeps the build without it)
#
# NEW helper written for the public repo (the original tree was assembled by
# hand with the same inputs). The result was checked to be byte-identical to
# the tree that was built on the tablet; this script itself has not been run.
# Run on any machine with git; copy the resulting directory to the tablet
# (the original build used /home/hypr/src/panvk-kbase).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-panvk-fourfect}
MESA_COMMIT=feeadb5f55f4ba14a5c5ddc84ba20177e419cffb
NOTES_COMMIT=08aa2e62580e447b28fe419d8995b84d8d6f1fff

test ! -e "$OUT" || { echo "$OUT already exists" >&2; exit 1; }
git init -q "$OUT"
git -C "$OUT" remote add origin https://gitlab.freedesktop.org/mesa/mesa.git
# Fetch only the pinned commit; fall back to a full fetch if the server refuses.
git -C "$OUT" fetch --depth 1 origin "$MESA_COMMIT" || git -C "$OUT" fetch origin
git -C "$OUT" checkout -q "$MESA_COMMIT"

NOTES=$(mktemp -d)
trap 'rm -rf "$NOTES"' EXIT
git init -q "$NOTES"
git -C "$NOTES" remote add origin https://github.com/Noysz/panvk-g99-jm.git
git -C "$NOTES" fetch --depth 1 origin "$NOTES_COMMIT" || git -C "$NOTES" fetch origin
git -C "$NOTES" checkout -q "$NOTES_COMMIT"

patches=("$NOTES"/patches-upstream/000[1-5]-*.patch "$HERE/patches/fourfect-kbase-only-build.patch")
if [ "${PANVK_APPS:-0}" = 1 ]; then
  patches+=("$HERE/patches/panvk-kbase-signalled-export.patch")
fi
for p in "${patches[@]}"; do
  echo "applying $(basename "$p")"
  git -C "$OUT" apply --check "$p"
  git -C "$OUT" apply "$p"
done
echo "Source ready in $OUT"
