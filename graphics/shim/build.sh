#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
# Originally linked with the Android clang r416183b ld.lld from the kernel toolchain.
# Any aarch64-capable ld.lld works; override with LD_LLD=/path/to/ld.lld.
LINKER=${LD_LLD:-$(command -v ld.lld)}
for source in egl-shim drm-no-cursor drm-trace; do
  clang --target=aarch64-linux-gnu -O2 -fPIC -shared -nostdlib -ffreestanding \
    -fno-stack-protector -fuse-ld="$LINKER" "$HERE/$source.c" -o "$HERE/$source.so"
done
sha256sum "$HERE"/*.so
