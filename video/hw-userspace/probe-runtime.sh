#!/bin/sh
# Orchestrator-only, target tablet. Validates Bionic linker closure; NEVER starts
# daemon main, loads drivers, or opens a video device. No host Android paths added.
set -eu
[ "$(id -u)" = 0 ] || { echo 'Requires root on the target tablet' >&2; exit 1; }
[ "$#" = 1 ] || { echo 'Usage: probe-runtime.sh /opt/.../private-runtime-root' >&2; exit 2; }
VCU_ROOT=$(realpath "$1")
case "$VCU_ROOT" in /opt/omarchy-tablet/*) ;; *) echo 'Root must be below /opt/omarchy-tablet' >&2; exit 2;; esac
[ -e "$VCU_ROOT/system/bin/linker" ] || [ -L "$VCU_ROOT/system/bin/linker" ]
[ -e "$VCU_ROOT/vendor/bin/v3avpud" ] || [ -L "$VCU_ROOT/vendor/bin/v3avpud" ]
[ -f "$VCU_ROOT/linkerconfig/ld.config.txt" ]
mkdir -p "$VCU_ROOT/proc" "$VCU_ROOT/dev" "$VCU_ROOT/data"
# Mounts die with this private mount namespace. No decoder devices are bound.
exec unshare --mount --propagation private --fork sh -eu -c '
  vcu_root=$1
  mount -t proc -o nosuid,nodev,noexec proc "$vcu_root/proc"
  : > "$vcu_root/dev/null"
  mount --bind /dev/null "$vcu_root/dev/null"
  exec env -i ANDROID_ROOT=/system ANDROID_DATA=/data \
    PATH=/system/bin:/vendor/bin \
    /usr/bin/chroot "$vcu_root" /system/bin/linker --list /vendor/bin/v3avpud
' sh "$VCU_ROOT"
