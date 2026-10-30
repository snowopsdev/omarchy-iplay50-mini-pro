#!/bin/bash
# boot/unpack-firmware.sh <iPlay50_mini_Pro_*.zip>: extract and unpack the vendor firmware package for reference.
# Output in <repo>/work/firmware/ (same layout the project used):
#   extract/                 boot.img vendor_boot.img dtbo.img vbmeta*.img lk.img preloader + scatter files
#   unpack-boot/             unpack_bootimg output (kernel, ramdisk) + mkbootimg-args.txt
#   unpack-vendor_boot/      vendor_ramdisk00, dtb, bootconfig ...
#   work-boot-kernel/Image, work-boot-ramdisk/ramdisk.cpio, work-vendor-ramdisk/vendor_ramdisk.cpio
#   vendor-ramdisk-list.txt  listing of the vendor ramdisk (nothing executed)
# Scripted from the commands in the project's firmware/NOTES.md. The real zip was deleted after extraction there, so this
# script was only tested (2026-10-30) on a stand-in zip holding the same extracted files under one folder.
#
# NOTE: the 2026-03-02 package (iPlay50_mini_Pro_V1.0_20260302) carries kernel 5.10.177 (ab10731447) and
# matching vendor modules. The project's tablet ran 5.10.149 (ab9703858). The notes record no flash of anything from it;
# it is reference material (partition table, boot header values, ramdisk layout for the test harness).
set -euo pipefail
ZIP=${1:?usage: unpack-firmware.sh <firmware.zip>}
REPO=$(cd "$(dirname "$0")/.." && pwd)
W=$REPO/work/firmware
U=${UNPACK_BOOTIMG:-$REPO/work/mkbootimg/unpack_bootimg.py}
mkdir -p "$W/extract"
unzip -j -o "$ZIP" '*/boot.img' '*/vendor_boot.img' '*/dtbo.img' '*/vbmeta.img' '*/vbmeta_system.img' \
	'*/vbmeta_vendor.img' '*/lk.img' '*/preloader_tb8781p1_64.bin' '*/MT6789_Android_scatter.xml' \
	'*/MT6789_Android_scatter.txt' '*/scatter_checksum.xml' -d "$W/extract" \
|| unzip -j -o "$ZIP" boot.img vendor_boot.img dtbo.img vbmeta.img vbmeta_system.img vbmeta_vendor.img lk.img \
	preloader_tb8781p1_64.bin MT6789_Android_scatter.xml MT6789_Android_scatter.txt scatter_checksum.xml -d "$W/extract"
(cd "$W/extract" && sha256sum * > SHA256SUMS.txt && cat SHA256SUMS.txt)
cd "$W"
mkdir -p unpack-boot
python3 -I "$U" --boot_img extract/boot.img --out unpack-boot > unpack-boot/info.txt
python3 -I "$U" --boot_img extract/boot.img --out unpack-boot --format mkbootimg > unpack-boot/mkbootimg-args.txt
python3 -I "$U" --boot_img extract/vendor_boot.img --out unpack-vendor_boot
mkdir -p work-boot-kernel work-boot-ramdisk work-vendor-ramdisk
gzip -dc unpack-boot/kernel > work-boot-kernel/Image
lz4 -dc unpack-boot/ramdisk > work-boot-ramdisk/ramdisk.cpio
lz4 -dc unpack-vendor_boot/vendor_ramdisk00 > work-vendor-ramdisk/vendor_ramdisk.cpio
bsdtar -tf work-vendor-ramdisk/vendor_ramdisk.cpio > vendor-ramdisk-list.txt
echo "boot.img header:   $(cat unpack-boot/mkbootimg-args.txt)"
echo "firmware kernel:   $(strings work-boot-kernel/Image | grep -m1 '^Linux version 5' | cut -c1-80)"
echo "vendor module vermagic: $(grep -a -o -m1 'vermagic=5\.10\.[0-9a-z.-]*' work-vendor-ramdisk/vendor_ramdisk.cpio)"
echo "Compare with your tablet's running kernel (uname -r). Only 5.10.149-...-ab9703858 is what this project rebuilt."
