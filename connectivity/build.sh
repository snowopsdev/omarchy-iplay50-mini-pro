#!/bin/sh
# Static aarch64/musl build of mtk-wmt-init (and mtk-bt-bridge).  No root needed.
# Needs: Android clang r416183b (has ld.lld; the same prebuilt used for the kernel build) and an Alpine musl-dev sysroot
# unpacked (signature-verified) in work/musl-sysroot:
#   curl -O https://dl-cdn.alpinelinux.org/alpine/latest-stable/main/aarch64/musl-dev-1.2.6-r3.apk
#   python3 -I tools/apkfetch.py musl-dev-1.2.6-r3.apk <alpine-devel@lists.alpinelinux.org-616ae350.rsa.pub> work/musl-sysroot
# Set CLANG_DIR to the r416183b directory (the one containing bin/clang).
# (Public-repo adaptation: the private tree hard-coded the clang location; paths are now taken from CLANG_DIR.)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
: "${CLANG_DIR:?set CLANG_DIR to Android clang r416183b (contains bin/clang)}"
CC=${CC:-$CLANG_DIR/bin/clang}
S=$HERE/work/musl-sysroot/usr
# compiler-rt soft-float (fp128) helpers needed by musl printf/scanf on aarch64; plain compiler runtime, no libc dependency
RT=${RT:-$CLANG_DIR/lib64/clang/12.0.5/lib/linux/libclang_rt.builtins-aarch64-android.a}
mkdir -p "$HERE/out" "$HERE/work/obj"
PROGS=${PROGS:-"mtk-wmt-init mtk-bt-bridge"}
for p in $PROGS; do
  [ -f "$HERE/src/$p.c" ] || continue
  $CC --target=aarch64-linux-musl -nostdlibinc -isystem "$S/include" -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
      -fno-stack-protector -fno-ident -ffunction-sections -fdata-sections -c -o "$HERE/work/obj/$p.o" "$HERE/src/$p.c"
  $CC --target=aarch64-linux-musl -nostdlib -static -fuse-ld=lld -Wl,--gc-sections -Wl,--build-id=none -Wl,-z,norelro \
      -o "$HERE/out/$p" "$S/lib/crt1.o" "$S/lib/crti.o" "$HERE/work/obj/$p.o" "$S/lib/libc.a" "$RT" "$S/lib/crtn.o"
  "$CLANG_DIR/bin/llvm-strip" --strip-all "$HERE/out/$p"
done
cd "$HERE/out" && sha256sum $PROGS | tee SHA256SUMS
