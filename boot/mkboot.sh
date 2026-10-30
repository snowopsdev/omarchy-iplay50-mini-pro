#!/bin/bash
# boot/mkboot.sh: wrap a gzip'd kernel Image into an Android boot image v4 for boot_a.
#   boot/mkboot.sh linux   [Image.gz] [out.img]   default: work/kernel/dist/Image.gz -> work/boot/boot-linux.img
#   boot/mkboot.sh stock   [Image]    [out.img]   default: work/kernel/ci/Image (Google's ab9703858 CI kernel)
#                                                 -> work/boot/boot-stock-restore.img (rollback, see docs/03)
# Header values copied from the vendor firmware's boot.img (v4, os 12.0.0, patch level 2023-08, empty cmdline).
# MTK lk ignores both the boot.img ramdisk and its cmdline on this device, so "linux" has no ramdisk and the
# stage0 initramfs + rdinit= live inside the kernel. "stock" optionally takes RAMDISK=<file> (the project used the
# firmware boot.img's ramdisk; lk ignores it anyway).
# Reproduces the project's images byte-for-byte from the same inputs (checked on the desktop 2026-10-30).
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
MKBOOTIMG=${MKBOOTIMG:-$REPO/work/mkbootimg/mkbootimg.py}
mode=${1:?usage: mkboot.sh linux|stock [kernel] [out.img]}
mkdir -p "$REPO/work/boot"
HDR=(--header_version 4 --os_version 12.0.0 --os_patch_level 2023-08 --cmdline '')
case $mode in
linux)
	K=${2:-$REPO/work/kernel/dist/Image.gz}; OUTIMG=${3:-$REPO/work/boot/boot-linux.img}
	python3 -I "$MKBOOTIMG" "${HDR[@]}" --kernel "$K" --output "$OUTIMG" ;;
stock)
	K=${2:-$REPO/work/kernel/ci/Image}; OUTIMG=${3:-$REPO/work/boot/boot-stock-restore.img}
	gzip -9 -n -c "$K" > "$OUTIMG.Image.gz"
	python3 -I "$MKBOOTIMG" "${HDR[@]}" --kernel "$OUTIMG.Image.gz" ${RAMDISK:+--ramdisk "$RAMDISK"} --output "$OUTIMG"
	rm -f "$OUTIMG.Image.gz" ;;
*) echo "usage: mkboot.sh linux|stock [kernel] [out.img]" >&2; exit 2 ;;
esac
[ "$(stat -c %s "$OUTIMG")" -le $((64 * 1024 * 1024)) ] || { echo "image larger than the 64 MiB boot partition" >&2; exit 1; }
sha256sum "$OUTIMG"
