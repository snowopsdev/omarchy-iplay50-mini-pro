#!/omarchy/bin/busybox sh
# Mount vendor_a / vendor_dlkm_a (single extents inside super, read-only) and locate scratch partitions.
part() { for d in /sys/class/block/*; do grep -q "^PARTNAME=$1$" $d/uevent 2>/dev/null && { echo /dev/$(basename $d); return; }; done; }
SUPER=$(part super)
mkdir -p /vendor /vendor_dlkm
grep -q " /vendor_dlkm " /proc/mounts || { L=$(losetup -f); losetup -r -o $((0x143800000)) $L $SUPER && mount -o ro $L /vendor_dlkm; }
grep -q " /vendor " /proc/mounts || { L=$(losetup -f); losetup -r -o $((0x106d00000)) $L $SUPER && mount -o ro $L /vendor; }
echo "LOGDEV=$(part vendor_boot_b)" > /run/omarchy-parts
grep -E " /vendor(_dlkm)? " /proc/mounts
cat /run/omarchy-parts
