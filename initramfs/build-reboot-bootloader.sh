#!/bin/bash
# Build the freestanding static aarch64 reboot-bootloader helper (no libc) with the kernel's clang-r416183b.
#   initramfs/build-reboot-bootloader.sh [out]     (default out: <repo>/work/initramfs/reboot-bootloader)
# Command line as recorded in the project's poc/NOTES.md. CLANG defaults to the toolchain kernel/build.sh fetches.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
CLANG=${CLANG:-$REPO/work/kernel/clang/r416183b}
OUT=${1:-$REPO/work/initramfs/reboot-bootloader}
mkdir -p "$(dirname "$OUT")"
"$CLANG/bin/clang" --target=aarch64-linux-gnu -O2 -ffreestanding -fno-stack-protector -fno-builtin -nostdlib -static \
	-fuse-ld=lld -Wl,--build-id=none -Wl,-z,norelro -o "$OUT" "$HERE/initramfs-src/src/reboot-bootloader.c"
"$CLANG/bin/llvm-strip" "$OUT"
file "$OUT" 2>/dev/null || true
sha256sum "$OUT"
