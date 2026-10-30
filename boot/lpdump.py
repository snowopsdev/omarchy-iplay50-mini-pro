#!/usr/bin/env python3
"""lpdump.py <super-head.bin | /dev/<super>> [slot]

Print the logical partitions inside an Android dynamic `super` partition (liblp metadata, slot 0 by default)
with their extents as byte offsets, i.e. what you need for
    losetup -r -o <offset> --sizelimit <size> /dev/loopN /dev/<super>
Only the first 384 KiB are read, so a dump is enough:
    dd if=/dev/sdc60 of=super-head.bin bs=4096 count=96      (on the tablet; find super by PARTNAME)

The project hard-coded vendor_a at 0x106d00000 and vendor_dlkm_a at 0x143800000 (rootfs/onboard/mount-vendor.sh).
Run this against your own tablet's super before trusting those numbers: an OTA can move them.
Partitions with more than one extent (system_a on the project's tablet: 7) cannot be loop-mounted; they need
device-mapper (dmsetup), which was never needed here.
"""
import struct
import sys

SECTOR = 512
RESERVED = 4096          # LP_PARTITION_RESERVED_BYTES
GEOMETRY_SIZE = 4096     # LP_METADATA_GEOMETRY_SIZE (primary + backup copy follow the reserved area)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    slot = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    with open(sys.argv[1], "rb") as f:
        data = f.read(RESERVED + 2 * GEOMETRY_SIZE + 4 * 65536 + 65536)
    g = RESERVED
    magic, _size = struct.unpack_from("<II", data, g)
    if magic != 0x616C4467:
        sys.exit("no LP geometry magic at 4096 - not a super partition?")
    meta_max, slots, lbs = struct.unpack_from("<III", data, g + 8 + 32)
    h = RESERVED + 2 * GEOMETRY_SIZE + slot * meta_max
    magic, major, minor, hdr_size = struct.unpack_from("<IHHI", data, h)
    if magic != 0x414C5030:
        sys.exit("no LP metadata header magic")
    off = h + 4 + 2 + 2 + 4 + 32 + 4 + 32
    tables = [struct.unpack_from("<III", data, off + 12 * i) for i in range(4)]  # partitions, extents, groups, block devs
    base = h + hdr_size
    (p_off, p_n, p_sz), (e_off, e_n, e_sz) = tables[0], tables[1]
    extents = []
    for i in range(e_n):
        nsec, ttype, tdata, tsrc = struct.unpack_from("<QIQI", data, base + e_off + i * e_sz)
        extents.append((nsec, ttype, tdata, tsrc))
    print(f"LP metadata v{major}.{minor}, {slots} slot(s), max metadata {meta_max} bytes, block size {lbs}; slot {slot}")
    print(f"{'name':<22}{'extents':>8}  {'offset':>14}  {'size':>14}  sectors")
    for i in range(p_n):
        e = base + p_off + i * p_sz
        name = data[e:e + 36].split(b"\0", 1)[0].decode()
        _attrs, first, num, _group = struct.unpack_from("<IIII", data, e + 36)
        if num == 0:
            print(f"{name:<22}{0:>8}  {'-':>14}  {'-':>14}  (empty)")
            continue
        for k, (nsec, ttype, tdata, _src) in enumerate(extents[first:first + num]):
            label = name if k == 0 else ""
            cnt = num if k == 0 else ""
            kind = "" if ttype == 0 else " (zero extent)"
            print(f"{label:<22}{cnt!s:>8}  {tdata * SECTOR:>#14x}  {nsec * SECTOR:>#14x}  {nsec}{kind}")


if __name__ == "__main__":
    main()
