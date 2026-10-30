# 02 - Rebuilding the tablet's own GKI kernel (5.10.149, build ab9703858)

Mainline Linux has no MT6631 Wi-Fi/Bluetooth, audio or suspend support for MT6789. The tablet's
Android kernel has all of them, as **vendor modules** that live on the tablet's own partitions. So
this project keeps that exact kernel: it rebuilds Google's GKI kernel for build **ab9703858** from
source, adds only the config options systemd needs that leave the module ABI untouched, and builds
the stage0 initramfs and `rdinit=/linuxinit` into it. Every stock vendor module then loads
unchanged.

## Status

| Item | State |
|---|---|
| Rebuild from Google's exact source + toolchain pins | Works |
| `vmlinux.symvers` vs Google CI: 6894/6894 export CRCs identical, file byte-identical | Verified (base, `+VT/PID_NS/USER_NS`, `+initramfs`, `+CMDLINE` builds) |
| Kernel banner byte-identical to stock | Verified |
| Boots on the tablet; all 171 first-stage + the needed `vendor_dlkm` modules load | Verified on hardware (2026-10-28) |
| Per-module `__versions` CRC check against the new `vmlinux.symvers` | Checked on 2026-10-30 for 6 connectivity modules from the tablet (all match). The other modules are covered by the identical `vmlinux.symvers` and by loading on the device |
| `CONFIG_SYSVIPC`, `CONFIG_POSIX_MQUEUE` | **Rejected**: they change 4642 / 4471 CRCs |
| `Image` byte-identical to stock | No, by design (extra features). Not a goal |
| `kernel/build.sh` as one script | Run on the desktop through the `config` step (output identical to the config that was booted). The build itself was run step by step during the project with the same commands |

## Prerequisites

- Your tablet runs `5.10.149-android12-9-00031-g8d44cb1a9e42-ab9703858` ([docs/01](01-device-and-backups.md), step 1).
- Desktop (x86_64 Linux; the project used 16 cores): `git curl make bc bison flex perl python3
  openssl lz4 cpio kmod gzip`, about 6 GB free (2.8 GB build tree, ~2 GB toolchain + source).
  No root needed. A full LTO+CFI build took about 12 minutes on 16 cores.
- For the initramfs that is built into the kernel: `qemu-aarch64` (qemu-user, to list the
  busybox applets), see [docs/03](03-boot-and-rootfs.md).

## Pins (from the build manifest)

`https://ci.android.com/builds/submitted/9703858/kernel_aarch64/latest/raw/manifest_9703858.xml`
(copy: `kernel/manifest_9703858.xml`)

| Project | Revision |
|---|---|
| `kernel/common` (branch `android12-5.10-2022-12`) | `8d44cb1a9e4253fe215bcc16435564f8d2e42333` |
| `platform/prebuilts/clang/host/linux-x86` | `6e3223f76384455acde43affde3df0ea9df66c0d`, directory `clang-r416183b` (Android 7284624, clang 12.0.5) |
| `kernel/build` | `cc6c1e7a6ef92244016312f53bd4694722ab3c14` (reference only, not used to build) |
| `platform/system/tools/mkbootimg` | `34bc8bfb493401f7aadbd3b434f9fbfa521e9301` |

CI artifacts used (`.../9703858/kernel_aarch64/latest/raw/<name>`): `vmlinux.symvers` (the ABI
reference), `abi_symbollist`, `Image` (stock kernel, for rollback). sha256 values are in
`kernel/build.sh`. Google does not publish a `boot.img` for this build.

## Steps

The kernel embeds the stage0 initramfs, so the initramfs is built in the middle. The toolchain is
needed for both.

```sh
kernel/build.sh fetch                       # source @8d44cb1a (shallow), clang-r416183b, CI artifacts (+sha256 check)
initramfs/fetch-alpine.sh                   # busybox-static, busybox-extras, musl (signature + sha256 checked)
initramfs/build-reboot-bootloader.sh        # tiny static helper, built with the same clang
python3 -I initramfs/build-initramfs.py     # -> work/initramfs/out/initramfs-builtin.cpio, stage1.cpio, ...
kernel/build.sh config build verify dist    # -> work/kernel/dist/{Image,Image.gz,vmlinux.symvers,config}
boot/fetch-mkbootimg.sh
boot/mkboot.sh linux                        # -> work/boot/boot-linux.img (flash: [docs/03](03-boot-and-rootfs.md))
```

What `kernel/build.sh` does, and what to expect:

1. **fetch.** `git fetch --depth 1` of the pinned commit. A shallow clone cannot run
   `git describe`, so it writes `src/.scmversion` = `-android12-9-00031-g8d44cb1a9e42` (the
   "-00031" counts commits since the KMI tag). Downloads the clang archive and checks that
   `clang --version` says `Android (7284624, based on r416183b) clang version 12.0.5`.
2. **config.**
   - KMI symbol list for `TRIM_UNUSED_KSYMS`: `kernel/symbollist.py` flattens the in-tree
     `android/abi_gki_aarch64*` lists named in `build.config.gki.aarch64` (the main list plus 21
     additional ones). Expected: 6894 symbols, identical to the CI `abi_symbollist`.
   - Stock config: `gki_defconfig` + `TRIM_UNUSED_KSYMS` + the CI whitelist path. This was checked
     on 2026-10-30 to be **identical** to the tablet's `/proc/config.gz`. To use your own tablet's
     file instead: `STOCK_CONFIG=stock-config kernel/build.sh config`.
   - Additions from `kernel/omarchy-tablet.config` (enabled with `scripts/config`, then
     `olddefconfig`), plus `CONFIG_INITRAMFS_SOURCE` and `CONFIG_CMDLINE` (stock value +
     ` rdinit=/linuxinit`).
   - Expected: `config: differs from stock exactly as kernel/config-vs-stock.diff`.
3. **build.** `make Image Image.lz4 modules` with the environment of Google's build
   (`LLVM=1 LLVM_IAS=1`, `KCFLAGS=-D__ANDROID_COMMON_KERNEL__`, `TZ=UTC`, build user/host
   `build-user@build-host`, `BUILD_NUMBER=9703858` which appends `-ab9703858`, and
   `KBUILD_BUILD_TIMESTAMP="Tue Mar  7 19:36:52 UTC 2023"`). Full LTO and CFI stay on, as in stock.
   Harmless noise: `which: no aarch64-linux-gnu-elfedit`, and openssl deprecation warnings.
4. **verify.** Expected output:

   ```text
   == release: 5.10.149-android12-9-00031-g8d44cb1a9e42 / utsrelease.h: 5.10.149-android12-9-00031-g8d44cb1a9e42-ab9703858
   == banner matches stock
   == ABI vs CI:
   stock=6894 mine=6894 identical=6894 changed=0 missing=0 added=0
   == vmlinux.symvers byte-identical to CI
   == built-in initramfs == .../initramfs-builtin.cpio
   ```

   `make kernelrelease` omits `-ab9703858`, as it does on Google's CI. `uname -r` on the tablet
   shows the full string.
5. **dist.** `Image.gz` = `gzip -9 -n` of `Image`. That is what goes into `boot.img`.

Reference hashes from the project (booted kernel, 2026-10-28): `Image`
`13fa3f939a2cc927cca17560853ea7ee84a17c62812a1204001c59de26714423`, `vmlinux.symvers`
`d9e16b693c2ce0743b1f53c8c490c4e2079ea246f1147db69c3e55596e5d0479` (= CI). Your `Image` will
differ, because the booted kernel embedded an earlier initramfs (cpio sha256
`f45eab6c...`). That cpio differs from today's `initramfs-builtin.cpio` (`4f0de2f3...`) only in
the fallback copy of `init-main`, which gained the reboot-mode module loading and the
`force-rescue` switch. Extracting the cpio from the booted `Image` and diffing it confirmed this.
`boot/mkboot.sh linux` reproduces the project's `boot-linux.img` byte for byte from the project's
`Image.gz`.

## Verify the vendor modules against your build (optional, recommended)

The project could not do this per module at build time (the module files were not on the
desktop). With Arch running ([docs/03](03-boot-and-rootfs.md)), copy the modules to the desktop and check every import CRC:

```sh
mkdir -p work/modules
tools/tablet 'tar -C /vendor_dlkm/lib/modules -cf - .' | tar -C work/modules -xf -
python3 -I kernel/check-module-crcs.py work/kernel/dist/vmlinux.symvers work/modules
#   OK  wlan_drv_gen4m_6789.ko: 255 vmlinux CRCs match, 0 differ, 69 from other modules
#   ...
#   vermagic seen: 5.10.149-android12-9-g496b43a2c455-dirty SMP preempt mod_unload modversions aarch64
```

(The first-stage modules exist only inside the initramfs, under `/lib/modules`.) Run against the
6 connectivity modules from the project's tablet: 0 mismatches. As a negative control, the same
modules against the `CONFIG_SYSVIPC` build's `vmlinux.symvers`: all 6 fail. Keep the copied
modules on your desktop; they are vendor files and must not be published.

With `MODVERSIONS`, the kernel checks the CRCs and ignores the version part of vermagic. So the
modules' `5.10.149-...-g496b43a2c455-dirty` vermagic is fine.

## Trying another config option

```sh
kernel/try-option.sh VT SYSVIPC POSIX_MQUEUE PID_NS USER_NS   # one full build per option
```

Each option is enabled alone on top of the configured tree, built, and compared with the CI
`vmlinux.symvers`. Any `changed=` other than 0 means the vendor modules would be rejected. Results
from the project:

| Option | Result |
|---|---|
| `DEVTMPFS`, `DEVTMPFS_MOUNT`, `FHANDLE`, `AUTOFS_FS` (systemd requirements) | identical (the base build) |
| `VT` | identical |
| `PID_NS` | identical |
| `USER_NS` | identical |
| `VT + PID_NS + USER_NS` together | identical, byte-identical symvers |
| `SYSVIPC` (+`IPC_NS`, `SYSVIPC_COMPAT`) | **4642 of 6894 changed** |
| `POSIX_MQUEUE` (+`IPC_NS`) | **4471 of 6894 changed** |
| `INITRAMFS_SOURCE`, `CMDLINE` | identical |

## Pitfalls and lessons

- **Only ABI-neutral options.** `MODVERSIONS=y`, `TRIM_UNUSED_KSYMS=y`, full LTO and CFI are all on
  in stock. Any option that changes a struct seen by exported symbols changes thousands of CRCs,
  and then no vendor module loads. Check every candidate with `try-option.sh`.
- **The release string has to match exactly.** It comes from three pieces: `.scmversion` (the
  shallow clone cannot `git describe`), `BUILD_NUMBER=9703858` (the `-ab` suffix) and
  `KBUILD_BUILD_TIMESTAMP` (the banner).
- **The symbol list matters.** `TRIM_UNUSED_KSYMS` with a different whitelist exports a different
  symbol set. The in-tree lists flatten to exactly the CI list (6894 symbols). `build.sh` checks
  both.
- **`olddefconfig` is a no-op on the stock config.** A control run that changed only the whitelist
  path gave a one-line diff. So every line of `config-vs-stock.diff` comes from the additions.
- **`CONFIG_CMDLINE` order in this tree.** `drivers/of/fdt.c` (`early_init_dt_scan_chosen`) with
  `CMDLINE_EXTEND` puts `CONFIG_CMDLINE` **first** and appends the DT `/chosen` bootargs after it.
  A bootloader-supplied `rdinit=` would therefore win over ours. lk does not supply one.
- **Incremental rebuilds after changing the initramfs.** Kbuild tracks the built-in cpio only by
  its path. Delete `out/usr/initramfs_data.o` and `out/usr/initramfs_inc_data` (`build.sh build`
  does it) or the old cpio stays in `Image`.
- `INITRAMFS_FORCE` stays unset. `populate_rootfs` unpacks the built-in cpio first, then the
  bootloader's ramdisks on top. That ordering is the basis of the design in [docs/03](03-boot-and-rootfs.md).
- The project deleted the 2.8 GB build tree after each build to save disk. The scripts rebuild
  it from `src/` and `clang/`.

## How it works

- GKI keeps a stable kernel module interface (KMI) per branch. Vendor modules are built against
  it and carry a CRC for every symbol they import. A kernel built from the same source, config and
  toolchain exports the same CRCs, so the tablet's own `vendor_boot` and `vendor_dlkm` modules load
  into the rebuilt kernel.
- The added options (`DEVTMPFS`, `FHANDLE`, `AUTOFS_FS`, `VT`, `PID_NS`, `USER_NS`) change no
  exported structure layout, so they leave the KMI alone. `SYSVIPC`/`POSIX_MQUEUE` add `IPC_NS`
  fields to shared structures and break it.
- Systemd needs `DEVTMPFS`, `FHANDLE` and `AUTOFS_FS`. The project's brief listed `VT`, `SYSVIPC`,
  `PID_NS`, `USER_NS` and `POSIX_MQUEUE` as further options missing from stock. The three that keep
  the ABI were added; the notes do not record which userspace component needed each one.

## Rollback

The stock kernel can be rebuilt into a flashable image from Google's CI `Image`:
`boot/mkboot.sh stock` (see [docs/03](03-boot-and-rootfs.md#recovery-and-rollback)). Kernel builds
never touch the tablet by themselves.
