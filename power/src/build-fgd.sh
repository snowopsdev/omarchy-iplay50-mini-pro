#!/bin/sh
# Build fgd-kernel-mode for the tablet (arm64).
#
# The original build used a static aarch64 musl toolchain (Android clang r416183b + an Alpine
# musl-dev sysroot). Any arm64 C compiler works; the program only needs libc + sockets:
#   on the tablet itself:   sh power/src/build-fgd.sh            (CC=cc, native)
#   cross from a desktop:   CC=aarch64-linux-gnu-gcc sh power/src/build-fgd.sh
# Output: power/out/fgd-kernel-mode (static if the toolchain supports -static).
# The non-musl builds were not tested on the device; the deployed binary was the musl build.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$HERE/../out}
CC=${CC:-cc}
mkdir -p "$OUT"
$CC -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter ${STATIC--static} \
    -o "$OUT/fgd-kernel-mode" "$HERE/fgd-kernel-mode.c"
file "$OUT/fgd-kernel-mode" 2>/dev/null || true
sha256sum "$OUT/fgd-kernel-mode"
