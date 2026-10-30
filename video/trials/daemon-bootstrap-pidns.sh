#!/bin/sh
set -eu
r=/opt/omarchy-tablet/video/vcu-root
mkdir -p "$r/sys"
trap 'echo 1 > /proc/sys/fs/binfmt_misc/qemu-arm' EXIT
echo 0 > /proc/sys/fs/binfmt_misc/qemu-arm
set +e
timeout --signal=TERM --kill-after=3s 12s unshare --mount --pid --mount-proc --propagation private --fork sh -eu -c '
 r=$1
 mount -t proc -o nosuid,nodev,noexec proc "$r/proc"
 mount --rbind /dev "$r/dev"
 mount --bind /sys "$r/sys"
 mount -o remount,bind,ro "$r/sys"
 exec strace -ff -tt -s 160 -o /opt/omarchy-tablet/video/v3avpud-pidns.strace \
  env -i ANDROID_ROOT=/system ANDROID_DATA=/data PATH=/system/bin:/vendor/bin \
  /usr/bin/chroot "$r" /vendor/bin/v3avpud -f
' sh "$r"
result=$?
echo "BOOTSTRAP_EXIT=$result"
exit 0
