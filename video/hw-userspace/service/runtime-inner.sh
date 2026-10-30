#!/bin/sh
# Target tablet only; called in fresh mount+PID namespaces by the supervisor.
set -eu
base=/opt/omarchy-tablet/video
root=/run/omarchy-tablet-vcu/root
mkdir -p "$root/system" "$root/vendor" "$root/apex/com.android.runtime" \
  "$root/proc" "$root/dev" "$root/data/vendor/vcodec" "$root/linkerconfig" \
  "$root/sys/firmware/devicetree/base" "$root/sys/kernel/mm/transparent_hugepage"
bind_ro() {
  mount --bind "$1" "$2"
  mount -o remount,bind,ro "$2"
}
bind_ro "$base/android-system/system" "$root/system"
bind_ro /vendor "$root/vendor"
bind_ro "$base/runtime-apex/mnt" "$root/apex/com.android.runtime"
# Candidate config already passed native linker preflight. Keep host paths intact.
: > "$root/linkerconfig/ld.config.txt"
bind_ro "$base/runtime/ld.config.txt" "$root/linkerconfig/ld.config.txt"
# Architecture config takes priority on some Bionic versions. If present, use
# the same already-validated config only in this private mount namespace.
if [ -e "$root/system/etc/ld.config.arm.txt" ]; then
  bind_ro "$base/runtime/ld.config.txt" "$root/system/etc/ld.config.arm.txt"
fi
mount -t proc -o nosuid,nodev,noexec proc "$root/proc"
mount -t tmpfs -o nosuid,noexec,mode=0755 tmpfs "$root/dev"
for device in vcu null zero urandom random; do
  test -c "/dev/$device"
  : > "$root/dev/$device"
  mount --bind "/dev/$device" "$root/dev/$device"
done
# /proc/device-tree is a symlink into this subtree. No writable host sysfs.
bind_ro /sys/firmware/devicetree/base "$root/sys/firmware/devicetree/base"
: > "$root/sys/kernel/mm/transparent_hugepage/enabled"
bind_ro /sys/kernel/mm/transparent_hugepage/enabled \
  "$root/sys/kernel/mm/transparent_hugepage/enabled"
exec env -i ANDROID_ROOT=/system ANDROID_DATA=/data PATH=/system/bin:/vendor/bin \
  /usr/bin/chroot "$root" /vendor/bin/v3avpud -f
