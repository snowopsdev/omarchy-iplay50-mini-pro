# 03 - Boot chain, initramfs and the Arch Linux ARM root filesystem

How the tablet gets from its MediaTek bootloader (lk) to systemd on an Arch Linux ARM (ALARM) root
filesystem on `userdata`, using the rebuilt GKI kernel from [docs/02](02-kernel.md) and the tablet's
own vendor modules. It also covers the USB debug link, the module load order, and how to recover.

Partitions written by this document: `boot_a`, `init_boot_b`, `userdata`. Read the brick risks in
[docs/01](01-device-and-backups.md#brick-risks-read-before-flashing-anything) first.

## Status

| Item | State |
|---|---|
| lk boots our kernel from `boot_a`, built-in initramfs started via `rdinit=/linuxinit` in `CONFIG_CMDLINE` | Works (verified on hardware, 2026-10-28) |
| stage0 (`/linuxinit`) loads the 171 first-stage vendor modules, runs stage1 from `init_boot_b` | Works |
| USB NCM link + telnet rescue shell in the initramfs | Works |
| ext4 root on `userdata`, `switch_root` to systemd, SSH over USB | Works |
| `vendor_a` / `vendor_dlkm_a` loop-mounted read-only from `super` offsets | Works |
| `vendor_dlkm` module groups (power, GPU, connectivity, audio, extras), `devices_deferred` empty | Works |
| Reboot into fastboot from Linux (needs `reboot-mode.ko` + `syscon-reboot-mode.ko`) | Works from stage1 / rescue (`reboot-bootloader`) |
| stage0's own 60 s timer reboot | Fires as designed; **whether it lands in fastboot is unverified** (stage0 does not load the reboot-mode modules) |
| Power-off charging boot (lk sets `androidboot.mode=charger`) | Hardware unit has a power-only path; a cold charging-mode boot was **not tested** in these notes ([docs/11](11-power-battery.md)) |
| pstore / ramoops as a crash log | **Not working** (the record never updated during the project's boots) |
| `resize2fs` of the 4 GiB image to the full partition | The step is recorded; its output is not |
| Going back to Android | **Unverified** (never attempted) |
| `mtk_disp_sec.ko` | Fails to load (needs `cmdq-sec-drv` / `trusted_mem` symbols); not needed |

## Prerequisites

- [docs/01](01-device-and-backups.md) done: same kernel build on your tablet, your data backed up,
  bootloader unlocked.
- [docs/02](02-kernel.md) built: `work/initramfs/out/stage1.cpio`, `work/kernel/dist/Image.gz`,
  `work/boot/boot-linux.img`.
- Desktop packages: `fastboot`, `python3`, `qemu-aarch64` (qemu-user), `fakeroot`, `e2fsprogs`
  (`mke2fs -d`), `img2simg` (from android-tools, or built from AOSP `libsparse` as the project
  did), `gpg`, `curl`, `lz4`, `bsdtar`; optional `socat` (for `tools/tsh-put`) and `telnet`.
- An SSH key of your own: `ssh-keygen -t ed25519 -f ~/.ssh/omarchy-tablet_ed25519` ([tools/README.md](../tools/README.md)).
- **Know how to reach fastboot without Linux.** The notes do not record a hardware key combination
  for this tablet. Find yours while Android still works (`adb reboot bootloader` is the software
  way) before you flash `boot_a`.

## Files

| Repo path | Ends up | Purpose |
|---|---|---|
| `initramfs/initramfs-src/linuxinit` | `/linuxinit` in the kernel's built-in cpio | **stage0** |
| `initramfs/initramfs-src/init-main` | `/init` of `stage1.cpio` on `init_boot_b`; fallback copy at `/omarchy/libexec/init-main` | **stage1**: USB link, find/mount `userdata`, `switch_root`, rescue |
| `initramfs/initramfs-src/diag-init` | `/init` of `stage1-diag.cpio` | Minimal diagnostic stage1: logs what it sees, reboots to fastboot after 20 s |
| `initramfs/initramfs-src/bin/usb-gadget` | initramfs; also `/usr/local/sbin/omarchy-usb-gadget` on the rootfs | configfs NCM/ECM gadget supervisor, 172.16.42.1 |
| `initramfs/initramfs-src/bin/{rescue-shell,continue-boot}` | initramfs | telnet login shell; "retry booting" |
| `initramfs/initramfs-src/src/reboot-bootloader.c` | initramfs | Static helper: sync, remount ro, `reboot(RESTART2, "bootloader")` |
| `initramfs/build-initramfs.py` | - | Writes the cpios without root (own newc writer) |
| `initramfs/rescue/{prep.sh,load.sh}` | pushed to the rescue shell by hand | Interactive `vendor_dlkm` module bring-up with a persisted log |
| `initramfs/tools/sim-overlay.py`, `initramfs/tools/test-harness/` | - | Offline simulation of the ramdisk overlay and of stage0/stage1 |
| `rootfs/fetch-alarm.sh`, `build-rootfs.sh`, `edit-rootfs.sh`, `make-rootfs-image.sh` | - | ALARM image, unprivileged |
| `rootfs/overlay/etc/...` | `/etc/...` on the rootfs | networkd link config, gadget service, sshd key-only, persistent journal |
| `rootfs/onboard/*` | `/opt/omarchy-tablet/` (+ unit, sysctl) | Vendor partition mounts and module loading at every boot (`omarchy-hw.service`) |
| `rootfs/deploy-onboard.sh` | - | Copies `rootfs/onboard` (plus extra dirs) to the tablet |
| `boot/lpdump.py` | - | Prints logical partition offsets inside `super` |

`connectivity/` ([docs/05](05-wifi-bluetooth.md)) and `sensors/` ([docs/06](06-sensors.md)) carry their own copies of
`load-conn-nowifi.list`, `load-sensors.list`, the USB-gadget files and `host-usbnet.sh`. They have
the same content as the copies here.

## Steps

### 1. Build the kernel, initramfs and boot image

As in [docs/02](02-kernel.md#steps). Outputs used below:

```text
work/boot/boot-linux.img             kernel only (no ramdisk, empty cmdline), boot image v4
work/initramfs/out/stage1.cpio       raw uncompressed newc cpio, must be <= 8 MiB (init_boot_b size)
work/initramfs/out/stage1-rescue.cpio same, but stays in the rescue shell (for repairs)
work/initramfs/out/stage1-diag.cpio  diagnostic stage1
```

With the same Alpine packages and reboot helper, `build-initramfs.py` reproduces the project's
flashed `stage1.cpio` byte for byte (sha256
`32d74a5cca8e673511dcc8e5bdaa08ccf77f2f9f6773a0a43ffbfe83fecbc8f7`). This was checked on
2026-10-30 from fresh downloads. `build-reboot-bootloader.sh` reproduces the helper too
(`24e53da3...`).

### 2. Build the root filesystem image

```sh
rootfs/fetch-alarm.sh          # ArchLinuxARM-aarch64-latest.tar.gz, md5 + GPG (key 68B3537F39A313B3E574D06777193F152BDBE6A6)
rootfs/build-rootfs.sh         # R1 extract under fakeroot (drops ALARM's mainline kernel/dtbs/modules), R2 edits
rootfs/make-rootfs-image.sh    # R3 -> work/rootfs/out/rootfs.ext4 (4 GiB) + rootfs.sparse.img
cp work/rootfs/hostkeys/known_hosts ~/.ssh/omarchy-tablet_known_hosts
```

The edits (`rootfs/edit-rootfs.sh`): hostname `omarchy-tablet`. The ALARM DHCP-everything
`eth.network`/`en.network` are removed and replaced by `10-usb-gadget.network` (static
172.16.42.1/24, gateway/DNS 172.16.42.2, matched by name *and* the gadget's fixed MACs). sshd
becomes key-only, with your public key for `root` and `alarm`. New SSH host keys are generated.
The journal is persistent and forwarded to kmsg. `omarchy-usb-gadget.service` is enabled. fstab
gets `LABEL=omarchy-root / ext4 rw,noatime,errors=remount-ro`. `sshd`, `systemd-networkd` and
`systemd-resolved` were already enabled in the ALARM tarball.
`make-rootfs-image.sh` runs `mke2fs -d` under the same fakeroot database, so ownership is root's
without sudo, and disables `orphan_file`, which the 5.10 kernel cannot mount. It then converts
the image to an Android sparse image.

### 3. Prepare the desktop end of the USB link

```sh
tools/host-usbnet.sh --install    # prints the one-time systemd-networkd install, or use --wait after boot (NetworkManager)
```

Details, addresses and MACs: [tools/README.md](../tools/README.md).

### 4. Flash

In fastboot on the tablet, with slot a active. Commands recorded in the notes are marked (R); the
`boot_a` command is not written out in the notes and is the standard form.

```sh
fastboot getvar current-slot                                   # must say a
fastboot flash init_boot_b work/initramfs/out/stage1.cpio       # (R) raw cpio, uncompressed
fastboot flash userdata work/rootfs/out/rootfs.sparse.img       # (R) DESTROYS Android data; fastboot splits it to max-download-size
fastboot flash boot_a work/boot/boot-linux.img
fastboot reboot
```

Order: stage1 and the rootfs go first, so the first boot of the new kernel finds both.

### 5. First boot: what happens and what to expect

There is no serial console and the screen stays dark (the desktop comes in docs 04 and 08). Watch
the USB link:

1. **stage0** (`/linuxinit`, inside the kernel) mounts proc/sys/devtmpfs and logs
   `<2>omarchy-stage0: alive`. It arms a 60 s "reboot to bootloader" timer, then `insmod`s every
   module in the vendor ramdisk's `/lib/modules/modules.load` (171 on the project's tablet, deps
   first, each logged *before* insmod). It finds `PARTNAME=init_boot_b`. If that starts with a newc
   magic (`070701`), it unpacks it to `/stage1`, syntax-checks `/stage1/init` and `exec`s it.
   Otherwise it runs the built-in copy of `init-main`.
2. **stage1** (`init-main`) disarms the timer first. It loads `reboot-mode.ko` +
   `syscon-reboot-mode.ko`, starts `usb-gadget watch` (gadget on `musb-hdrc`, IP 172.16.42.1,
   telnetd on port 23) and arms its own 150 s watchdog. It waits up to 100 s for
   `PARTNAME=userdata`, mounts it ext4 `rw,noatime` on `/newroot`, saves its log to
   `/newroot/var/log/initramfs.log`, moves `/dev /proc /sys` and runs `switch_root /newroot /sbin/init`.
3. **systemd** on ALARM: `omarchy-usb-gadget.service` adopts the existing gadget (the link
   survives the switch), networkd configures 172.16.42.1, sshd starts.

Verify from the desktop:

```sh
tools/host-usbnet.sh --wait
tools/tablet 'uname -r; systemctl is-system-running; tail -n 20 /var/log/initramfs.log'
#   5.10.149-android12-9-00031-g8d44cb1a9e42-ab9703858
tools/tablet "journalctl -k -b | grep -E 'omarchy-(stage0|init|usb)' | head -n 40"
```

If the link does not come up within about a minute, see "Rescue shell" below. If nothing
enumerates at all, see "Recovery".

### 6. Right after the first boot

```sh
boot/backup-partitions.sh                       # recommended, see [docs/01](01-device-and-backups.md) (not done in the project)
tools/tablet 'resize2fs "$(findmnt -no SOURCE /)"'   # (R: "resize2fs /dev/sdcN") grow ext4 to the whole ~228 GiB partition
```

The 4 GiB image has `resize_inode`, so it can grow online to the full partition.
`findmnt` is a convenience; the notes give `/dev/sdcN` (the project's userdata was `sdc61`).

ALARM ships with its well-known default passwords for `root` and `alarm`. sshd here accepts keys
only, but set real passwords (or lock the accounts) before the tablet is on any other network. For
pacman on this kernel (`DisableSandbox`, `IgnorePkg = linux-aarch64`) and the keyring, see
[docs/04](04-omarchy.md).

### 7. Vendor partitions and modules at every boot

`rootfs/onboard/hw-up.sh` (run by `omarchy-hw.service`, `WantedBy=sysinit.target`, before
networking and Bluetooth) does this:

1. Sets the Type-C `preferred_role` to `sink`.
2. `mount-vendor.sh` loop-mounts `vendor_a` → `/vendor` and `vendor_dlkm_a` → `/vendor_dlkm`
   (read-only, `losetup -r -o OFFSET`). It also records `vendor_boot_b` as `LOGDEV`.
3. In charger mode (`androidboot.mode = "charger"` in `/proc/bootconfig`) it loads only the power
   lists, sets the backlight to 5 % and stops.
4. Otherwise it sets the backlight to 40 % and loads, in order: `load-power.list load-gpu.list
   load-audio.list load-extra.list load-conn-nowifi.list`, then `load-sensors.list`
   ([docs/06](06-sensors.md)). Then come Wi-Fi (`wifi-up.sh` with `mtk-wmt-init`) and Bluetooth
   (`bt-up.sh` with `mtk-bt-bridge`) from [docs/05](05-wifi-bluetooth.md). Missing Wi-Fi/BT files
   only produce a `hw-up: wifi-up failed` line in kmsg.

**Check the `super` offsets on your own tablet first.** `mount-vendor.sh` hard-codes the project's
values. An OTA can move them:

```sh
tools/tablet 'u=$(grep -lx PARTNAME=super /sys/class/block/*/uevent); dd if=/dev/$(basename $(dirname $u)) bs=4096 count=96 status=none' > work/super-head.bin
python3 -I boot/lpdump.py work/super-head.bin
#   vendor_a        1   0x106d00000   0x3c96a000  1985360
#   vendor_dlkm_a   1   0x143800000    0x237d000    72680
```

(`boot/lpdump.py` was checked against the project's own `super` header dump: identical offsets,
and `system_a` shows its 7 extents.) If yours differ, edit the two offsets in
`rootfs/onboard/mount-vendor.sh`.

Install and enable:

```sh
rootfs/deploy-onboard.sh [connectivity/... dirs with the [docs/05](05-wifi-bluetooth.md) binaries/scripts]
tools/tablet 'systemctl enable omarchy-hw.service && systemctl reboot'     # enable command not recorded verbatim
```

Verify after the reboot:

```sh
tools/tablet 'grep -c "^ok" /run/omarchy-hw/load.log; grep FAIL /run/omarchy-hw/load.log; findmnt /vendor; findmnt /vendor_dlkm'
tools/tablet 'cat /sys/kernel/debug/devices_deferred; cat /proc/asound/cards; ls /dev/mali0 /dev/dri'
```

Expected on the project's tablet: no `FAIL` lines, `devices_deferred` empty, ALSA card
`mt6789-mt6366`, `/dev/mali0`, DRM `card0` with connector DSI-1 (1200x1920), backlight at
`/sys/class/leds/lcd-backlight`, touch input `mtk-tpd`.

Vendor files the other docs need come from these mounts on **your** tablet: firmware under
`/vendor/firmware`, init scripts under `/vendor/etc/init`, modules under `/vendor_dlkm/lib/modules`.
Copy them to your desktop the same way, e.g.
`tools/tablet 'tar -C /vendor/etc -cf - init' | tar -C work/vendor-etc -xf -`. They are
proprietary: keep them out of any public repository.

## Module loading

Where the modules come from:

- **First stage** (171 modules on the project's tablet): the vendor ramdisk in `vendor_boot_a`,
  which lk unpacks on top of our initramfs as `/lib/modules` with `modules.load`, `modules.dep`
  and `modules.load.recovery`. stage0 loads exactly `modules.load`. USB, UFS, display
  (`mediatek-drm`, DSI panel), touch (`mtk-tpd`) and keys come from here. All loaded.
- **Second stage**: `/vendor_dlkm/lib/modules` (161 entries in its `modules.load`). Android loaded
  337 modules in total at runtime (185 from `vendor_dlkm`). Loading all 161 at once **reset the
  tablet via the watchdog**. The project instead loads hand-made, dependency-sorted groups
  (`rootfs/onboard/load-*.list`). `load-modules.sh` skips already loaded modules, so shared
  dependencies may appear in several lists.

Results on 2026-10-28: power 29 ok, GPU ok, connectivity 13 ok (including the ccci modem
drivers, no hang), audio 19 ok. The extras in `load-extra.list` were needed on top:
`nvmem-mt635x-efuse` (unblocks the codec and auxadc), `mtk_gpufreq_mt6789`, `mali_mgm_mt6789`,
`mali_prot_alloc_mt6789`, `mtk-btcvsd`, `mtk-scp-vow`, `mtk-vow`, `mtk-dvfsrc-start`, `fhctl`.

**Never load:** `clk-disable-unused.ko`, `mtk-pm-domain-disable-unused.ko`. They are the prime
suspects for the watchdog reset during the bulk load; by their names they switch off clocks and
power domains that look unused at that moment.

**Avoided / untested:** `gz_*` (GenieZone), `iommu_gz`, `devmpu`, `widevine`, `camera*`, `c2k*`,
the remaining `ccci_*`, `mtk_usb_f_rndis` / `mtk_u_ether` (they clash with our gadget).
`mtk_disp_sec` fails and is not needed.

**Reboot into fastboot** needs `reboot-mode.ko` and `syscon-reboot-mode.ko`. They are only in
`modules.load.recovery`, so stage1, `diag-init` and `load-power.list` load them explicitly.
Mechanism: `syscon-reboot-mode` on TOPRGU (0x10007000), offset 0x24, mask 0xf, `bootloader` = 3.
lk then enters fastboot.

**Wi-Fi ordering:** `wmt_chrdev_wifi` and `wlan_drv_gen4m_6789` must load only after
`mtk-wmt-init`'s loader stage, because the EMI base is latched at module init. That is why the
boot list is `load-conn-nowifi.list` and `wifi-up.sh` loads the rest ([docs/05](05-wifi-bluetooth.md)).

### Finding module groups yourself (how the lists were made)

From the stage1 rescue shell (below), push `initramfs/rescue/prep.sh`, `initramfs/rescue/load.sh`
and a list with `tools/tsh-put`, then:

```sh
sh prep.sh                 # loop-mount vendor + vendor_dlkm, note vendor_boot_b as LOGDEV
sh load.sh mylist.list     # insmod one by one; the log goes to /run/load.log AND is written raw
                           # to the start of vendor_boot_b after every step
```

Writing the log to the unused `vendor_boot_b` after every module means a hard reset still leaves
the last attempted module on flash. Read it back from the next rescue shell, e.g.
`head -c 65536 /dev/<vendor_boot_b> | strings` (the read-back command is not recorded). The notes
call the lists "dependency-sorted" against `vendor_dlkm`'s `modules.dep`. How they were sorted is
not recorded. `/proc/modules` from Android ([docs/01](01-device-and-backups.md), step 1) lists
what Android loaded.

## Rescue shell and debugging

- While in the initramfs: `telnet 172.16.42.1` (root, no password, USB only). Fallback:
  `nc 172.16.42.1 2323`. Non-interactive: `tools/tsh 'cmd'`; file upload: `tools/tsh-put`.
  The rescue banner lists the useful commands.
- The init log is in `/run/initramfs.log` (ring buffer, last ~1500 lines). Every line also goes to
  kmsg as `<2>omarchy-init: ...` (stage0: `omarchy-stage0`, gadget: `omarchy-usb`). After
  `switch_root` the log is appended to `/var/log/initramfs.log` on the rootfs.
- The `stage1.cpio` build sets `etc/verbose`, so stage1 runs with `set -x` traced into kmsg until it
  reaches the rescue loop (`STAGE1_TRACE=0` turns that off).
- Ways to stay in the initramfs: the file `/.stay-in-initramfs` on the root filesystem; `rescue` or
  `omarchy.rescue=1` on the kernel command line, or `rescue` in bootconfig; or flash
  `stage1-rescue.cpio` to `init_boot_b`. In rescue the root filesystem stays mounted at `/newroot`.
- `continue-boot` (in the rescue shell) asks PID 1 to retry mount + `switch_root`.
  `continue-boot -f` first removes `/newroot/.stay-in-initramfs`.
- `touch /omarchy/etc/no-autoreboot` in the rescue shell stops the 150 s watchdog from rebooting
  (until the next boot). `NO_AUTOREBOOT=1 python3 -I initramfs/build-initramfs.py` bakes that
  in, but only takes effect after a kernel rebuild, since that file lives in the built-in cpio.
- `stage1-diag.cpio` on `init_boot_b`: logs block devices with their `PARTNAME`, UDC state,
  deferred probes and the dmesg tail to kmsg, then reboots into fastboot after 20 s. It is for when
  stage1 itself is in doubt.
- Test changes offline before flashing:

  ```sh
  export HARNESS_WORK=$PWD/work/test-harness; mkdir -p $HARNESS_WORK
  OUT=$HARNESS_WORK/out REBOOT_HELPER=initramfs/tools/test-harness/stub-rbh python3 -I initramfs/build-initramfs.py
  initramfs/tools/test-harness/setup.sh          # merges ours + vendor ramdisk + generic ramdisk like the kernel does
  initramfs/tools/test-harness/go.sh $HARNESS_WORK/out/stage1.cpio 30
  ```

  This runs stage0 → stage1 in user/mount/pid namespaces with stub `insmod`/`reboot` and a fake
  `init_boot_b`. It uses the firmware package's ramdisks (`boot/unpack-firmware.sh`) for the
  layout. Checked on 2026-10-30 with `stage1-diag.cpio`.

## Recovery and rollback

Built-in safety nets:

| Situation | What happens |
|---|---|
| `init_boot_b` missing, not a newc cpio, or `/stage1/init` fails `sh -n` | stage0 runs the built-in `init-main` instead |
| stage1 never disarms stage0's timer within 60 s | stage0 runs `reboot-bootloader`. Without the reboot-mode modules (stage0 does not load them) this is probably a plain reboot. **Unverified** |
| No USB host session and no root mounted within 150 s (stage1) | State dump to kmsg (`omarchy-dump[...]`), then reboot into fastboot |
| Root mounted but `switch_root` fails | Rescue shell; the watchdog is re-armed (fastboot after 150 s without a USB session) |
| Root mounted, systemd misbehaves later | No automatic action. Use the stay-in-initramfs options above |

Recipes:

- **Broken root filesystem or systemd config:** `fastboot flash init_boot_b
  work/initramfs/out/stage1-rescue.cpio`, `fastboot reboot`, `telnet 172.16.42.1`, fix files under
  `/newroot`, then `continue-boot`. Flash the normal `stage1.cpio` back afterwards.
- **Reach fastboot from a running system:** in the rescue shell, `reboot-bootloader /newroot`
  (it syncs and remounts `/newroot` read-only first). On Arch, `hw-up.sh` has loaded the
  reboot-mode modules (`load-power.list`). `systemctl reboot --reboot-argument=bootloader` should
  then work. **Not tested.**
- **Bad stage1:** reflash a known-good `stage1.cpio` to `init_boot_b`. Keep the last working one.
- **Bad kernel:** reflash the previous `boot-linux.img` to `boot_a`. If the kernel does not
  boot at all, nothing of ours runs and you need fastboot from the bootloader (key combination:
  see Prerequisites).
- **Start over with the root filesystem:** reflash `userdata` (everything on it is lost).
- **Back to Android (unverified):** `boot/mkboot.sh stock` wraps Google's CI `Image` for
  ab9703858 (the same build the tablet ran) into `work/boot/boot-stock-restore.img`. The project
  built this image (sha256 `3eade57e...`, with the firmware ramdisk via `RAMDISK=`); the notes
  record no flash of it. The original `boot_a` was never backed up, so whether it was
  byte-identical is unknown. Then flash it to `boot_a` and wipe Android's data
  from the **bootloader**, not fastbootd: `fastboot -w` then `fastboot erase metadata`. On this
  tablet, `fastboot -w` in fastbootd silently did nothing; this was learned while flashing GSIs
  before this project. `init_boot_b` / `vendor_boot_b` can keep our data; slot a does not read them.

## Pitfalls and lessons

- **lk takes the kernel from `boot_a` but the ramdisk from `init_boot_a` (+ the `vendor_boot`
  ramdisk), and drops the `boot.img` header cmdline.** Verified on hardware. Two intermediate
  images show the path there: one put the initramfs in the boot image's ramdisk, the next put
  `rdinit=/linuxinit` in the header cmdline. lk ignores both fields on this tablet. Hence an
  initramfs built into the kernel (`CONFIG_INITRAMFS_SOURCE`) plus `rdinit=` in `CONFIG_CMDLINE`.
- **Android's ramdisks are unpacked on top of ours.** The vendor ramdisk has `bin -> /system/bin`
  and `etc -> /system/etc` symlinks, and the generic ramdisk replaces `/init`. They own `/bin /etc
  /sbin /init /system /lib`. Everything of ours therefore lives under `/omarchy` plus
  `/linuxinit`. busybox-extras (dynamic, musl) runs through `/omarchy/lib/ld-musl-aarch64.so.1`
  directly, so `/lib` is never touched. `sim-overlay.py` replays the kernel's unpacking order
  offline.
- **stage1 on `init_boot_b` is how to iterate quickly:** `fastboot flash init_boot_b stage1.cpio`
  replaces the boot logic without a kernel rebuild. It must be a raw, *uncompressed* newc cpio of
  at most 8 MiB. stage0 also accepts it wrapped in an Android boot header (cpio at +4096).
- **Never write to `/dev/console`** (it may block). All init output goes to `/dev/kmsg` at level
  `<2>` so it reaches every console. **PID 1 never exits:** every failure path ends in a parking
  loop or the rescue loop.
- **No serial console, and pstore/ramoops was not reliable.** What worked: kmsg lines, the saved
  `/var/log/initramfs.log`, the per-step log on `vendor_boot_b`, and a persistent journal with
  `ForwardToKMsg=yes`.
- **Watchdog:** the hardware watchdog is kicked by the vendor `aee_hangdet` kernel threads (`wdtk`),
  and `hang_detect` is effectively disabled (countdown 2^31). Those threads and `ccci_poll1` sit
  in D state: 11 D-state threads gave a high load average while the CPU was ~97 % idle. That is
  not real load.
- **Bulk-loading vendor modules reset the tablet.** Load in small groups and log each module
  before its `insmod`. Never load the two `*-disable-unused` modules.
- **musb quirks:** write `1` to `/sys/module/musb_hdrc/parameters/kernel_init_done` (the vendor
  init does the same at early-init) and `cmode=1`. `usb-gadget` sets `musb_force_on=1` once if no
  host session appears after 15 polling rounds (about 30 s), and swaps NCM ↔ ECM when the host
  enumerates but never brings the carrier up (about 16 s).
- **Type-C role:** the port sometimes stayed source/host, so there was no USB link and the battery
  drained. Replugging after `preferred_role=sink` fixed it ([docs/11](11-power-battery.md)).
- **ext4 features:** e2fsprogs ≥ 1.47 enables `orphan_file` by default, and 5.10 cannot mount it.
  `make-rootfs-image.sh` disables it (and `metadata_csum_seed`).
- `edit-rootfs.sh` is not idempotent (it appends to fstab and regenerates host keys).
  `build-rootfs.sh` refuses to run twice on the same tree.
- **ALARM's `linux-aarch64` kernel, dtbs and modules are useless here** and are left out of the
  image. Keep pacman from reinstalling them ([docs/04](04-omarchy.md)).
- **Test harness:** never run the real `reboot-bootloader` in a test. In a pid namespace,
  `reboot(RESTART2)` kills the namespace's init with SIGHUP; it happened once. Use `stub-rbh`.
- **Deploying to a running tablet:** `scp` cannot overwrite an executable that is running (the
  Wi-Fi/BT daemons). `deploy-onboard.sh` uploads to a staging directory and renames.
- A kernel without `DEVTMPFS` would still work: `init-main` falls back to tmpfs `/dev` + `mdev -s`.
  The rebuilt kernel has `DEVTMPFS`, so that path is not used.

## How it works

```text
lk ──> kernel (boot_a: GKI ab9703858 rebuild, CONFIG_CMDLINE "... rdinit=/linuxinit")
        populate_rootfs: 1. built-in cpio  (/linuxinit, /omarchy/**: busybox, telnetd, musl loader, init-main)
                         2. vendor ramdisk (vendor_boot_a: /lib/modules + modules.load, bin/etc symlinks)
                         3. generic ramdisk (init_boot_a: Android /init etc.)
        exec /linuxinit = stage0
          ├─ 60 s fallback timer
          ├─ insmod /lib/modules per modules.load (171)
          └─ init_boot_b (raw newc) -> /stage1 -> exec /stage1/init = stage1 (init-main)
               ├─ kill timer, load reboot-mode modules
               ├─ usb-gadget watch: NCM/ECM on musb-hdrc, 172.16.42.1, telnetd :23
               ├─ 150 s watchdog -> dump + reboot to fastboot if no USB session and no root
               ├─ mount PARTNAME=userdata (ext4) on /newroot
               └─ switch_root /newroot /sbin/init  (or rescue loop)
systemd (ALARM) ──> omarchy-usb-gadget.service (adopts gadget), networkd, sshd
               ──> omarchy-hw.service: hw-up.sh: loop-mount vendor_a/vendor_dlkm_a from super,
                   load module groups, Wi-Fi/BT bring-up ([docs/05](05-wifi-bluetooth.md))
```

Why two stages: changing the built-in initramfs needs a kernel rebuild and a `boot_a` flash.
stage0 is deliberately tiny and stable. All logic that changed during bring-up lives in stage1 on
a spare 8 MiB partition that fastboot can rewrite in seconds.

### What the vendor init scripts taught (summary)

The project read the tablet's `/vendor/etc/init/*.rc` (read-only, extracted from its own
device as above). What mattered for a plain-Linux boot:

- Android loads most vendor modules through `insmod_sh` (`/vendor/bin/init.insmod.sh` with
  `/vendor/etc/init.insmod.${ro.hardware}.cfg`). Neither file was in the reviewed backup. The
  `vendor.all.modules.ready` gate is set to 1 later in the same early-init trigger, so it never
  waits. In practice, Android's runtime `/proc/modules` (337 entries) was the only complete
  reference. Hence the hand-made groups.
- USB: early-init writes `musb_hdrc` `kernel_init_done=1` (`musb_force_on` and `cmode` are
  commented out). The UDC is `musb-hdrc`. Android's gadget (`/config/usb_gadget/g1`, VID 0x0E8D)
  offers only `mtk_rndis` as a network function, and no rc file mounts configfs. This project uses
  the GKI kernel's built-in configfs NCM/ECM instead and leaves the vendor RNDIS modules unloaded.
- Connectivity: `connfem` and `wmt_drv` load at boot. The vendor binaries `wmt_loader` and
  `wmt_launcher -p /vendor/firmware/ -o 1` must run before the Wi-Fi/BT modules
  (`vendor.connsys.driver.ready=yes`). Replaced by `mtk-wmt-init` ([docs/05](05-wifi-bluetooth.md)).
- The Mali `insmod` is commented out in the rc files (the module is loaded anyway at runtime), and
  all four amplifier drivers (`aw87xxx`, `aw883xx`, `rt5512`, `tfa98xx`) were loaded at runtime
  with nothing saying which one is bound ([docs/07](07-audio.md), [docs/08](08-gpu.md)).
