#!/bin/bash
# boot/backup-partitions.sh [PARTNAME...]: copy raw partitions from the RUNNING tablet (Arch, over tools/tablet)
# to <repo>/work/backup/<name>.img (+ SHA256SUMS). Read-only on the tablet (dd from the block device).
#
# NOT part of what the project did: its tablet never had these backups (fastboot fetch is unsupported, the attempt
# left 0-byte files). Written for reproducers and not run on a device. Run it right after the first successful
# Linux boot: this guide never writes the partitions in the default list, so they still hold the original data.
# boot_a is NOT in the list: by then it holds your kernel (see docs/03 for the stock-kernel image instead).
#
# The default list includes per-device calibration / identity data (nvram, nvdata, nvcfg, persist, proinfo,
# protect1/2, seccfg): treat the dumps as private (MAC addresses, serial, IMEI live there). Never publish them.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
OUT=$REPO/work/backup
mkdir -p "$OUT"; chmod 700 "$OUT"
names=("$@")
[ ${#names[@]} -gt 0 ] || names=(init_boot_a vendor_boot_a dtbo_a vbmeta_a vbmeta_system_a vbmeta_vendor_a lk_a \
	nvram nvdata nvcfg persist proinfo protect1 protect2 seccfg)
for n in "${names[@]}"; do
	# find the block device by PARTNAME (device numbering is not stable across firmware) and stream it
	"$REPO/tools/tablet" "u=\$(grep -lx 'PARTNAME=$n' /sys/class/block/*/uevent | head -n1); [ -n \"\$u\" ] || { echo 'no PARTNAME=$n' >&2; exit 3; }; dd if=/dev/\$(basename \$(dirname \$u)) bs=1M status=none" \
		> "$OUT/$n.img"
	echo "$n: $(stat -c %s "$OUT/$n.img") bytes"
done
(cd "$OUT" && sha256sum ./*.img > SHA256SUMS && cat SHA256SUMS)
