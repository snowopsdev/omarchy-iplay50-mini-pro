#!/bin/bash
# setup.sh: fresh merged tree = our kernel-embedded cpio (built with the STUB reboot helper) + Android vendor ramdisk
# + generic ramdisk, unpacked in kernel order by sim-overlay.py; insmod/reboot replaced by tracing stubs.
#   build the stub cpio first:  OUT=$HARNESS_WORK/out REBOOT_HELPER=initramfs/tools/test-harness/stub-rbh python3 -I initramfs/build-initramfs.py
#   VENDOR_RAMDISK_CPIO / GENERIC_RAMDISK_CPIO: uncompressed cpios (boot/unpack-firmware.sh writes both to work/firmware/)
# (Adapted from the project's poc/tools/test-harness/setup.sh: paths made configurable.)
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$H/../../.." && pwd)
T=${HARNESS_WORK:-$REPO/work/test-harness}
VR=${VENDOR_RAMDISK_CPIO:-$REPO/work/firmware/work-vendor-ramdisk/vendor_ramdisk.cpio}
GR=${GENERIC_RAMDISK_CPIO:-$REPO/work/firmware/work-boot-ramdisk/ramdisk.cpio}
cd "$T"; rm -rf merged; mkdir merged
python3 -I "$H/../sim-overlay.py" merged out/initramfs-builtin.cpio "$VR" "$GR"
cd merged; rm omarchy/bin/insmod omarchy/bin/reboot
printf '#!/omarchy/bin/busybox sh\necho "$1" >> /run/insmod.trace\n[ -n "$SLOWMOD" ] && case "$1" in *musb_main*) sleep $SLOWMOD;; esac\nexit 0\n' > omarchy/bin/insmod
printf '#!/omarchy/bin/busybox sh\necho "STUB reboot -f" >> /run/reboot.trace\nsleep 3600\n' > omarchy/bin/reboot
chmod +x omarchy/bin/insmod omarchy/bin/reboot
mkdir -p fakeblk/sdc1 fakeblk/sdc2 fakeblk/sdc3
printf 'MAJOR=8\nMINOR=33\nDEVNAME=fakeblk0\nPARTNAME=init_boot_b\n' > fakeblk/sdc1/uevent
printf 'MAJOR=8\nMINOR=34\nDEVNAME=fakeblk1\nPARTNAME=init_boot_a\n' > fakeblk/sdc2/uevent
printf 'MAJOR=8\nMINOR=35\nDEVNAME=sdc9\nPARTNAME=userdata\n' > fakeblk/sdc3/uevent
cp "$H/run.sh" run.sh; chmod +x run.sh
