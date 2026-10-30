#!/bin/sh
# go.sh <stagefile|none> <seconds> [VAR=val...]   (runs inside the merged tree made by setup.sh)
# stagefile = what the fake init_boot_b partition contains (e.g. the stage1.cpio built with the stub helper).
# Unprivileged: user+mount+pid+net namespaces (unshare -rmpfn). Adapted from poc/tools/test-harness/go.sh.
H=$(cd "$(dirname "$0")" && pwd)
T=${HARNESS_WORK:-$H/../../../work/test-harness}
cd "$T/merged" || exit 1
f=$1; secs=$2; shift 2
rm -f dev/fakeblk0; [ "$f" != none ] && cp "$f" dev/fakeblk0
[ "$f" = none ] && SB=/nonexistent || SB=/fakeblk
exec timeout 120 unshare -rmpfn sh -c "touch dev/null; mount --bind /dev/null dev/null; mount --bind dev dev; env SYSBLOCK=$SB $* chroot . /omarchy/bin/busybox sh /run.sh $secs"
