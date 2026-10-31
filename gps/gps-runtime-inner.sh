#!/bin/sh
# Builds a private Bionic root for the vendor GPS daemon (mnld) and runs it.
# Call in fresh mount + PID namespaces (unshare -m -p -f --mount-proc).
# Reuses the read-only Android system image and runtime APEX of the VCU
# runtime (video/), adds the VNDK v31 APEX and the property shim.
set -eu
video=/opt/omarchy-tablet/video
g=/opt/omarchy-tablet/gps
root=/run/omarchy-tablet-gps/root
log=${GPS_LOG:-/run/omarchy-tablet-gps/mnld.log}
mkdir -p "$root/system" "$root/vendor" "$root/apex/com.android.runtime" \
  "$root/apex/com.android.vndk.v31" "$root/proc" "$root/dev" "$root/data" \
  "$root/linkerconfig" "$root/mnt/vendor/nvdata" "$root/sys" "$root/tmp" \
  "$root/gps-shim"
bind_ro() { mount --bind "$1" "$2"; mount -o remount,bind,ro "$2"; }
bind_ro "$video/android-system/system" "$root/system"
bind_ro /vendor "$root/vendor"
bind_ro "$video/runtime-apex/mnt" "$root/apex/com.android.runtime"
bind_ro "$g/vndk-v31/mnt" "$root/apex/com.android.vndk.v31"
: > "$root/linkerconfig/ld.config.txt"
bind_ro "$g/ld.config.txt" "$root/linkerconfig/ld.config.txt"
bind_ro "$g/shim" "$root/gps-shim"
bind_ro /mnt/vendor/nvdata "$root/mnt/vendor/nvdata"
mount -t proc -o nosuid,nodev proc "$root/proc"
mount -t sysfs -o ro,nosuid,nodev,noexec sysfs "$root/sys"
mount -t tmpfs -o nosuid,mode=0755 tmpfs "$root/dev"
mount -t tmpfs -o nosuid,nodev tmpfs "$root/tmp"
mount -t tmpfs -o nosuid,nodev,mode=0771 tmpfs "$root/data"
mkdir -p "$root/dev/socket" "$root/data/vendor/gps"
for d in stpgps stpgps2 gps_pwr gps_emi fw_log_gps null zero urandom random; do
  test -c "/dev/$d"
  : > "$root/dev/$d"; mount --bind "/dev/$d" "$root/dev/$d"
done
"$g/logd-sink.py" "$root/dev/socket/logdw" "$log" &
for _ in 1 2 3 4 5 6 7 8 9 10; do [ -S "$root/dev/socket/logdw" ] && break; sleep 0.1; done
exec env -i LD_PRELOAD=/gps-shim/lib64/libgpsprops.so ANDROID_ROOT=/system \
  ANDROID_DATA=/data PATH=/system/bin:/vendor/bin \
  /usr/bin/chroot "$root" /vendor/bin/mnld
