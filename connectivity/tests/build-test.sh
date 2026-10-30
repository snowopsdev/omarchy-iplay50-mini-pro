#!/bin/sh
# builds work/t_mtk: mtk-wmt-init with ioctl() replaced by tests/fake_ioctl.c (emulated kernel); then:
#   python3 -I tests/daemon-test.py work/t_mtk <fake-fwdir> <tmpdir>
# (Public-repo adaptation: clang location comes from CLANG_DIR, as in ../build.sh.)
set -eu
HERE=$(cd "$(dirname "$0")/.." && pwd)
: "${CLANG_DIR:?set CLANG_DIR to Android clang r416183b (contains bin/clang)}"
C=${CC:-$CLANG_DIR/bin/clang}; S=$HERE/work/musl-sysroot/usr
RT=$CLANG_DIR/lib64/clang/12.0.5/lib/linux/libclang_rt.builtins-aarch64-android.a
mkdir -p $HERE/work/obj
$C --target=aarch64-linux-musl -nostdlibinc -isystem $S/include -O1 -Dioctl=fake_ioctl -c -o $HERE/work/obj/t_main.o $HERE/src/mtk-wmt-init.c
$C --target=aarch64-linux-musl -nostdlibinc -isystem $S/include -O1 -c -o $HERE/work/obj/t_fake.o $HERE/tests/fake_ioctl.c
$C --target=aarch64-linux-musl -nostdlib -static -fuse-ld=lld -o $HERE/work/t_mtk $S/lib/crt1.o $S/lib/crti.o $HERE/work/obj/t_main.o $HERE/work/obj/t_fake.o $S/lib/libc.a $RT $S/lib/crtn.o
