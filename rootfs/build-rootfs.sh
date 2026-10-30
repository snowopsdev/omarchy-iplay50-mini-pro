#!/bin/bash
# rootfs/build-rootfs.sh: R1 + R2 of the project's poc/NOTES.md, unprivileged (fakeroot keeps ownership in state.db).
#   R1  extract the ALARM tarball, dropping the mainline linux-aarch64 kernel/initramfs/dtbs/modules (useless here:
#       the tablet boots its own GKI kernel and loads the vendor modules from its own partitions)
#   R2  edit-rootfs.sh: hostname, USB-gadget network + service, key-only sshd with your key, fresh host keys,
#       persistent journal mirrored to kmsg, fstab root by LABEL=omarchy-root
# Then: rootfs/make-rootfs-image.sh (R3). Work dir: <repo>/work/rootfs (tree/ is ~2 GB).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
W=$REPO/work/rootfs
cd "$W"
if [ -e .tree-edited ]; then
	echo "tree/ was already edited (edit-rootfs.sh is NOT idempotent: it appends to fstab, regenerates host keys)." >&2
	echo "Remove $W/tree, $W/state.db and $W/.tree-edited to start over." >&2; exit 1
fi
rm -rf tree state.db .tree-edited; mkdir -p tree
fakeroot -s state.db -- tar --warning=no-unknown-keyword -xpf dl/ArchLinuxARM-aarch64-latest.tar.gz --numeric-owner -C tree \
	--exclude='./boot/Image*' --exclude='./boot/initramfs-linux.img' --exclude='./boot/dtbs/*' --exclude='./usr/lib/modules/*'
fakeroot -i state.db -s state.db -- "$HERE/edit-rootfs.sh" "$W/tree" "$REPO"
: > .tree-edited
echo "rootfs tree ready: $W/tree   known_hosts line: ${HOSTKEY_DIR:-$REPO/work/rootfs/hostkeys}/known_hosts"
