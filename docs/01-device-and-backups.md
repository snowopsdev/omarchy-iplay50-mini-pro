# 01 - Device facts, unlocking, partitions and backups

This project runs Arch Linux ARM natively on the Alldocube iPlay 50 mini Pro, using the tablet's own
Android GKI 5.10 kernel (rebuilt from Google's source, [docs/02](02-kernel.md)) and the stock vendor
modules and firmware that are already on the tablet ([docs/03](03-boot-and-rootfs.md)). This document
covers what you must know and check **before** writing anything to the tablet.

Read the whole "Brick risks" section first. Several partitions on this tablet cannot be backed up
with fastboot, and the vendor download does not contain them.

## Status

| Item | State |
|---|---|
| Device identification (`fastboot getvar all`, kernel release) | Verified on the project's tablet |
| Bootloader unlock | Was already unlocked before the project. **How it was unlocked is not recorded** |
| Vendor firmware package download + unpack | Works (`iPlay50_mini_Pro_V1.0_20260302`), but it is a **newer build** than the project's tablet ran (see below) |
| `fastboot fetch` (read partitions back) | **Not supported** by this lk |
| Backups of `init_boot_a`, `vendor_boot_a`, original `boot_a` | **Not made** in the project (the attempt left 0-byte files) |
| Partition map (scatter file + on-device `PARTNAME`) | Verified |
| `boot/backup-partitions.sh` (dump partitions from running Linux) | Written for reproducers, **not run** on a device |
| Restoring Android after the port | **Unverified** (never attempted) |

## Device facts (project's tablet)

| | |
|---|---|
| Model | Alldocube iPlay 50 mini Pro, model T811M, box CUL8JN, codename Zhangwanmini, lk product `tb8781p1_64` |
| SoC | MediaTek MT8781 (= MT6789, Helio G99), arm64; GPU Mali-G57 MC2 |
| Display | 1200x1920 DSI panel |
| Storage | UFS; partitions appear as `/dev/sdcN` under Linux |
| Android | Android 13 vendor (`first-api-level 33`, VNDK 31), A/B with slot **a** active, dynamic `super` |
| Kernel | `5.10.149-android12-9-00031-g8d44cb1a9e42-ab9703858` = Google GKI build **ab9703858** |
| Boot partitions | `boot` 64 MiB, `init_boot` 8 MiB, `vendor_boot` 64 MiB, `dtbo`, `vbmeta`, `lk` per slot |
| `super` | 9 GiB (0x240000000); holds `system`, `vendor`, `product`, `vendor_dlkm`, `odm_dlkm` (slot a only) |
| `userdata` | ~228 GiB (0x38FD7F8000, "~245 GB"), f2fs, Android metadata-encrypted. **Wiped and reused as the Linux root** |
| fastboot | lk fastboot: `max-download-size 0x10000000` (256 MiB). fastbootd also works. `fastboot fetch` unsupported. `fastboot boot` reportedly unreliable on MTK lk (not used) |
| Bootloader state | `unlocked: yes`, `secure: yes` |

Before the port, the project's tablet ran a crDroid GSI on `system` over the stock vendor
partitions. Under TrebleDroid-based GSIs Android may report the bootloader as "locked/green";
that is spoofing. Trust `fastboot getvar unlocked`.

Selected values from `fastboot getvar all` (in fastbootd) on the project's tablet:

```text
current-slot:a              slot-count:2              super-partition-name:super
max-download-size:0x10000000 product:iPlay_50_mini_Pro unlocked:yes   secure:yes
partition-size:boot_a:0x4000000  partition-size:vendor_boot_a:0x4000000
partition-size:super:0x240000000 partition-size:userdata:0x38FD7F8000
security-patch-level:2023-04-05  version-vndk:31   version-os:12
system-fingerprint:Alldocube/iPlay_50_mini_Pro/iPlay_50_mini_Pro:13/TP1A.220624.014/1687767699:user/release-keys
```

## Prerequisites

- An unlocked bootloader. The project's tablet was already unlocked; the notes do not record how.
  The usual Android route is "OEM unlocking" in Developer options followed by
  `fastboot flashing unlock`. That route is **not verified** on this tablet, and unlocking wipes
  Android data.
- Desktop: `fastboot` (Google platform-tools or your distribution's android-tools). The project
  flashed from a Linux desktop without sudo. One macOS machine never enumerated the tablet on its
  direct ports and dropped the connection in fastboot through a dock.
- For the firmware package (optional reference, see below): `megatools`, `unzip`, `lz4`,
  `bsdtar` (libarchive), `python3`, and AOSP `mkbootimg` (`boot/fetch-mkbootimg.sh`).

## Step 1 - Check that your tablet runs the same kernel build

Everything in docs 02-03 assumes GKI build **ab9703858**. The vendor modules on your tablet must
match the kernel ABI you rebuild. From Android (adb with USB debugging):

```sh
adb shell uname -r            # expect 5.10.149-android12-9-00031-g8d44cb1a9e42-ab9703858
adb shell cat /proc/version   # banner: (build-user@build-host) ... clang version 12.0.5 ... Tue Mar  7 19:36:52 UTC 2023
```

If the string differs (the 2026-03-02 vendor package below carries `5.10.177-...-ab10731447`),
the approach still applies in principle: look up that build's manifest
(`https://ci.android.com/builds/submitted/<N>/kernel_aarch64/latest/raw/manifest_<N>.xml`),
pin the same projects and re-run the ABI comparison. **That was not done in this project.**

Also save, while Android still runs (useful references, all stay on your desktop):

```sh
adb shell zcat /proc/config.gz > stock-config      # the running kernel's config ([docs/02](02-kernel.md) can regenerate it too)
adb shell cat /proc/modules    > proc-modules.txt  # what Android loads at runtime (337 modules on the project's tablet)
fastboot getvar all 2> getvar.txt                  # in fastboot / fastbootd
```

The project had `stock-config` "from /proc/config.gz" and a `proc-modules.txt`; the exact commands
used to capture them are not recorded. Reading them as the adb shell user may need root on some
builds.

## Step 2 - Back up your data; understand what is lost

- Flashing the Linux root filesystem to `userdata` ([docs/03](03-boot-and-rootfs.md)) **destroys all
  Android user data** (apps, accounts, photos). Android's data is metadata-encrypted, so it cannot
  be recovered from the raw partition afterwards. Copy what you need off the tablet first.
- `fastboot fetch` is not supported, so fastboot cannot back up any partition. The project's
  attempt produced 0-byte `vendor_boot_a.img` / `vendor_boot_b.img` files. Unless your Android is
  rooted (then `dd` from `/dev/block/by-name/...` in a root shell; not done here), the first
  chance to dump partitions is the first Linux boot.
- **Recommended, not done in the project:** right after Linux first boots ([docs/03](03-boot-and-rootfs.md), step 6), dump
  the partitions this guide never writes:

  ```sh
  boot/backup-partitions.sh        # default: init_boot_a vendor_boot_a dtbo_a vbmeta*_a lk_a
                                   #          nvram nvdata nvcfg persist proinfo protect1 protect2 seccfg
  ```

  It streams each partition with `dd` over `tools/tablet` into `work/backup/` and writes
  `SHA256SUMS`. The calibration/identity partitions (`nvram`, `nvdata`, `nvcfg`, `persist`,
  `proinfo`, `protect1/2`, `seccfg`) hold per-device data such as MAC addresses, serial and IMEI:
  keep those files private.

## Partition layout

From the vendor scatter file (`MT6789_Android_scatter.xml`, UFS entries, all in `UFS_LU2` except
the preloaders), with what this project does to each. "In package" means the vendor zip contains an
image for it.

| Partition | Size | In package | Project's use |
|---|---|---|---|
| `boot_a` | 64 MiB | yes (but 5.10.177) | **Written**: our kernel (`boot-linux.img`) |
| `init_boot_a` | 8 MiB | **no** | **Never write.** lk takes the ramdisk from here |
| `vendor_boot_a` | 64 MiB | yes (but 5.10.177) | **Never write.** First-stage vendor modules + `modules.load` + DTB that our stage0 uses |
| `dtbo_a`, `vbmeta*_a`, `lk_a`, `tee_a`, `gz_a`, `scp_a`, `sspm_a`, `md1img_a`, ... | small | yes | Never written |
| `boot_b`, `vendor_boot_b`, `init_boot_b`, other `_b` | same sizes | no (marked invisible) | `init_boot_b` = **stage1 cpio**; `vendor_boot_b` = scratch log of `initramfs/rescue/load.sh`. Slot b is never booted |
| `super` | 9 GiB | yes | Read-only: `vendor_a` and `vendor_dlkm_a` are loop-mounted from it |
| `userdata` | rest (~228 GiB) | yes (resized) | **Written**: ext4 root filesystem, label `omarchy-root` |
| `metadata` | 32 MiB | no | Untouched (Android's encryption metadata) |
| `nvram`, `nvdata`, `nvcfg`, `persist`, `proinfo`, `protect1/2`, `seccfg`, `otp`, `frp`, `para`, `misc` | | no | Untouched. Per-device data: never write |
| `preloader` (LU0/LU1) | 1 MiB | yes | Never written |

Device node numbers seen on the project's tablet: `super`=sdc60, `userdata`=sdc61,
`vendor_boot_a`=sdc29, `init_boot_a`=sdc30, `vendor_boot_b`=sdc53, `nvdata`=sdc6, `nvram`=sdc35,
`persist`=sdc14, `metadata`=sdc13 (for LU2 partitions this is the scatter index minus 2). All
scripts here look partitions up by name instead (`PARTNAME=` in `/sys/class/block/*/uevent`).
Do the same.

Inside `super` (slot-0 metadata, project's tablet), printed by `boot/lpdump.py`:

| Logical partition | Offset in super | Size | Extents |
|---|---|---|---|
| `vendor_a` | 0x106d00000 | 0x3c96a000 (1985360 sectors) | 1 |
| `vendor_dlkm_a` | 0x143800000 | 0x237d000 (72680 sectors) | 1 |
| `system_a` | - | - | 7 (needs device-mapper; never needed) |
| `product_a`, `odm_dlkm_a`, `system_b` | | | 1 each |

## Brick risks (read before flashing anything)

- **Never write `init_boot_a`.** lk loads the boot ramdisk from it (not from `boot_a`). The vendor
  package contains no `init_boot.img`, and fastboot cannot read it back, so a bad write has no
  recovery source unless you dumped it yourself.
- **Never write `vendor_boot_a`.** It holds the 171 first-stage vendor modules, their
  `modules.load`, and the device tree. The package's `vendor_boot.img` belongs to the newer
  5.10.177 build, not to the ab9703858 kernel the project's tablet ran.
- **Never `fastboot set_active b`.** Slot b has no bootable system; on the project's tablet
  `init_boot_b` holds the stage1 cpio and `vendor_boot_b` may hold a debug log.
- Leave `lk`, `preloader`, `vbmeta*`, `dtbo`, `tee`, `md1img`, `nvram`/`nvdata`/`nvcfg`/`persist`/
  `proinfo`/`protect*`/`seccfg` alone. The project never wrote them.
- The only partitions this guide writes are `boot_a`, `init_boot_b` and `userdata`.
- Whether `vbmeta` verification had been disabled earlier on the project's tablet (it had run GSIs)
  is not recorded. The project's `boot-linux.img` carries no AVB signature (4 KiB of zeros) and lk
  booted it.

## Fastboot notes from earlier Android (GSI) flashing on this tablet

Before the Linux port the same tablet ran crDroid GSIs (2026-10-27). Lessons from that work:

- `fastboot -w` in **fastbootd** silently does nothing ("partition not found"). Wipe from the
  **bootloader** (lk) instead: `fastboot -w` plus `fastboot erase metadata`.
- `fastboot flash system` works in fastbootd.
- Flash from a Linux desktop with Google's platform-tools (no sudo needed). One macOS machine
  never enumerated the tablet on its direct ports, and through a USB dock it dropped out of
  fastboot.
- Stock recovery enumerates as `18d1:d001` "iPlay 50 mini Pro" and shows as adb
  "unauthorized"; that is not Android waiting for a debugging prompt.
- A TrebleDroid GSI reports the bootloader as "locked/green": that is spoofing. Trust
  `fastboot getvar unlocked`.

## The vendor firmware package (optional reference)

The vendor's download page (`https://www.alldocube.com/en/software/14927/`) listed
"iPlay50_mini_Pro_V1.0_20260302", 2.45 GB, Android 13, hosted on Mega:

```sh
# metadata only (name + size), no download; ours, uses the public Mega API + openssl
python3 -I boot/mega_info.py 'https://mega.nz/file/4YlzWaTJ#KQt3meF5-A-cog_nKLLEC__lHF9855SLB6Ktd4076L8'
#   -> iPlay50_mini_Pro_V1.0_20260302.zip, 2627314749 bytes
megatools dl --no-progress --path work/firmware/archive \
  'https://mega.nz/file/4YlzWaTJ#KQt3meF5-A-cog_nKLLEC__lHF9855SLB6Ktd4076L8'
# zip sha256 as downloaded 2026-10-27:
#   c2f1629934bf33523ebe82c552c856762b4c2035dafe2e3257b70d5a43952521
boot/fetch-mkbootimg.sh
boot/unpack-firmware.sh work/firmware/archive/iPlay50_mini_Pro_V1.0_20260302.zip
```

The project built megatools 1.11.5.20250706 from source (`xff.cz/megatools/builds`, signature
checked with key `EBFBDDE11FB918D44D1F56C1F9F0A873BE9777ED`). A second, older link
(`iPlay50miniProNFE(T811MAN)_EN_20231201.rar`) appeared in a user comment and was not used.

What the package is good for here:

- `MT6789_Android_scatter.xml`: the partition table above.
- The `boot.img` header values (`--header_version 4 --os_version 12.0.0 --os_patch_level 2023-08
  --cmdline ''`), reused by `boot/mkboot.sh`.
- The vendor ramdisk layout, for the offline initramfs simulator (`initramfs/tools/test-harness`).

What it is **not**: a match for the project's tablet. Its kernel is
`5.10.177-android12-9-00002-g593c61caffd9-ab10731447` and its vendor modules have vermagic
`5.10.177-android12-9-g222ed9da4a8c-dirty`. The project's tablet ran 5.10.149 with modules whose
vermagic is `5.10.149-android12-9-g496b43a2c455-dirty`. Do not flash its `boot.img` or
`vendor_boot.img` onto a 5.10.149 tablet. The package also has no `init_boot.img`, `nvram` or
`persist` images. Flashing the complete package with MediaTek's flash tool would be the
last-resort full restore. **That was not tried.**

## Rollback overview

See [docs/03](03-boot-and-rootfs.md#recovery-and-rollback) for the details. In short: Linux can be
reflashed from fastboot at any time (`boot_a`, `init_boot_b`, `userdata`). Going back to Android
needs a stock kernel on `boot_a` (`boot/mkboot.sh stock` rebuilds one from Google's CI `Image` for
ab9703858) plus a wiped `userdata`/`metadata`. Neither step was tried in this project.

## How the pieces fit

```text
preloader -> lk (fastboot lives here)
   lk loads: kernel  <- boot_a        (our GKI rebuild, initramfs + rdinit= built in)
             ramdisk <- init_boot_a   (Android generic ramdisk, untouched)
                      + vendor_boot_a (vendor ramdisk: first-stage modules, untouched)
             DTB     <- vendor_boot_a / dtbo_a (untouched)
   kernel -> /linuxinit (stage0, ours) -> stage1 from init_boot_b -> ext4 root on userdata
             vendor + vendor_dlkm loop-mounted read-only from super
```
