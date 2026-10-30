#!/bin/bash
# Build work/rootfs/out/rootfs.ext4 (raw, default 4 GiB) + rootfs.sparse.img from work/rootfs/tree, preserving the
# fakeroot ownership. (Adapted from the project's poc/make-rootfs-image.sh: paths; img2simg taken from $IMG2SIMG or PATH.)
set -euo pipefail
REPO=$(cd "$(dirname "$0")/.." && pwd)
W=$REPO/work/rootfs
SIZE=${SIZE:-4G}
IMG2SIMG=${IMG2SIMG:-img2simg}
mkdir -p "$W/out"
rm -f "$W/out/rootfs.ext4" "$W/out/rootfs.sparse.img"
cd "$W"
# orphan_file (default in e2fsprogs >= 1.47) is unsupported by the 5.10 kernel -> disable explicitly
fakeroot -i state.db -- mke2fs -q -t ext4 -b 4096 -L omarchy-root -m 1 \
	-O ^orphan_file,^metadata_csum_seed -E root_owner=0:0,lazy_itable_init=1,lazy_journal_init=1 \
	-d "$PWD/tree" "$W/out/rootfs.ext4" "$SIZE"
"$IMG2SIMG" "$W/out/rootfs.ext4" "$W/out/rootfs.sparse.img"
ls -ls "$W/out/rootfs.ext4" "$W/out/rootfs.sparse.img"
