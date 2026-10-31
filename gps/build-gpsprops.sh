#!/bin/sh
# Build the Bionic property shim libgpsprops.so (desktop, no NDK needed).
#   CLANG_DIR=/path/to/clang-r416183b gps/build-gpsprops.sh   -> gps/out/libgpsprops.so
# The shim uses no Bionic headers. A throwaway stub libc.so is linked only so
# the output gets DT_NEEDED libc.so (Bionic resolves open/read/write/close
# from the real libc at run time).
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
CC="${CLANG_DIR:?set CLANG_DIR to clang-r416183b}/bin/clang"
OUT="$HERE/out"; STUB=$(mktemp -d)
trap 'rm -rf "$STUB"' EXIT
mkdir -p "$OUT"
T="--target=aarch64-linux-android31 -fPIC -shared -nostdlib -fuse-ld=lld"
printf 'int open(const char*p,int f,...){return -1;}\nlong read(int a,void*b,unsigned long c){return -1;}\nlong write(int a,const void*b,unsigned long c){return -1;}\nint close(int a){return -1;}\n' > "$STUB/libc.c"
# shellcheck disable=SC2086
"$CC" $T -Wl,-soname,libc.so -o "$STUB/libc.so" "$STUB/libc.c"
# shellcheck disable=SC2086
"$CC" $T -O2 -ffreestanding -fno-stack-protector -Wl,-soname,libgpsprops.so \
  -Wl,-z,noexecstack -o "$OUT/libgpsprops.so" "$HERE/gpsprops.c" "$STUB/libc.so"
sha256sum "$OUT/libgpsprops.so"
