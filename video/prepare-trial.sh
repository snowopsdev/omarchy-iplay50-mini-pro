#!/bin/sh
set -eu
base=/opt/omarchy-tablet/video
test "$(blockdev --getsize64 /dev/sdc60)" = 9663676416
if ! dmsetup info omarchy-system-ro >/dev/null 2>&1; then
  dmsetup create --readonly omarchy-system-ro < "$base/system-ro.table"
fi
dmsetup mknodes omarchy-system-ro
test "$(blockdev --getro /dev/mapper/omarchy-system-ro)" = 1
mkdir -p "$base/android-system" "$base/runtime-apex/mnt"
mountpoint -q "$base/android-system" || mount -t ext4 -o ro,noload /dev/mapper/omarchy-system-ro "$base/android-system"
mountpoint -q "$base/runtime-apex/mnt" || mount -t ext4 -o ro,noload,loop "$base/runtime-apex/apex_payload.img" "$base/runtime-apex/mnt"
r=$base/vcu-root
mountpoint -q "$r/system" || mount --bind "$base/android-system/system" "$r/system"
mountpoint -q "$r/vendor" || mount --bind /vendor "$r/vendor"
mountpoint -q "$r/apex/com.android.runtime" || mount --bind "$base/runtime-apex/mnt" "$r/apex/com.android.runtime"
for mod in mtk-vcodec-common vcp gz_trusty_mod gz_irq_mod gz_ipc_mod gz_virtio_mod gz_tz_system iommu_gz trusted_mem mtk_sec_heap gz_main_mod cmdq-sec-drv mtk-vcu; do
  name=$(printf '%s' "$mod" | tr '-' '_')
  if ! test -d "/sys/module/$name"; then
    echo "LOAD $mod"
    insmod "/vendor_dlkm/lib/modules/$mod.ko"
  fi
done
test -c /dev/vcu
ls -l /dev/trusty* /dev/vcu 2>/dev/null || true
readlink /sys/bus/platform/devices/trusty/trusty:gz-main/driver
