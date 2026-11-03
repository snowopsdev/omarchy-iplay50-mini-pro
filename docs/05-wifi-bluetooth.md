# 05 - Wi-Fi, Bluetooth and the USB debug link

The tablet's Wi-Fi/Bluetooth is MediaTek "connsys" inside the MT6789 SoC with an MT6631
A-die (firmware family `soc2_2`, pattern `1d_1`). It runs on the stock vendor modules
(`wmt_drv`, `wmt_chrdev_wifi`, `wlan_drv_gen4m_6789`, `bt_drv_connac1x`, `btif_drv`,
`connadp`, `connfem`). On Android, three vendor userspace programs bring it up:
`wmt_loader`, `wmt_launcher` and `wlan_assistant`. This project replaces them with one
small static program, `mtk-wmt-init`, written from disassembly of those binaries and from
MediaTek's GPL kernel-module source. A `/dev/stpbt` to BlueZ bridge,
`mtk-bt-bridge`, provides Bluetooth.

## Status

| Item | State |
|---|---|
| Connsys power-on + ROM-patch download by `mtk-wmt-init` | Works (every boot via the hardware unit) |
| `wlan0` from the vendor gen4m driver | Works: `WMT turn on WIFI success!`, `wlanProbe: probe success` in the boot log |
| Factory MAC / NVRAM push from the `nvdata` partition (read-only) | Works: boot log shows `Using host-supplied MAC address` |
| WPA2-PSK association + DHCP through NetworkManager | Works, survives reboot. Omarchy's network panel manages it |
| WPA3/SAE and automatic PMF negotiation | **Failed** with this driver. The profile is pinned to WPA2 RSN, PMF disabled |
| `iwd` | **Does not work**: the kernel has no AF_ALG (`CONFIG_CRYPTO_USER_API_*` not set) |
| Bluetooth function on (`/dev/stpbt` open) at boot | Works: `BT_open: WMT turn on BT OK!`, `Now it's in MTK Bluetooth Mode` |
| `hci0` via `mtk-bt-bridge`, `bluetoothd`, pairing, BT audio | **Unverified.** No accessory test. Do not infer success from `hci0` alone. On 2026-10-28 WirePlumber also reported its BlueZ SPA plugin missing |
| Bridge controller quirks (0x0c77, 0xFC1A BD address, LE command bits) | Copied from a sibling-chip port, **unverified on MT6631** |
| Wi-Fi P2P / AP modes (`P`/`S`/`A`) | Untested |
| Wi-Fi after suspend, display-blank power cycling | Untested / unverified |
| USB CDC-NCM debug link `172.16.42.1` | Works. It was used throughout the project. ECM fallback implemented, not known to have been needed |

## Prerequisites

- The boot chain and vendor hardware bring-up from docs 01-03: vendor modules from
  your own `vendor_dlkm`, and `/vendor` and `/vendor_dlkm` mounted read-only. In this
  project they are loop mounts of the `vendor_a` / `vendor_dlkm_a` extents inside `super`
  (offsets in [docs/03](03-boot-and-rootfs.md)).
- Files that come from **your own device** (not shipped here):
  - `/vendor/firmware/WMT_SOC.cfg`, `soc2_2_ram_{mcu,bt,wifi}_1d_1_hdr.bin`,
    `WIFI_RAM_CODE_soc2_2_1d_1.bin` (optional: `wifi.cfg`, `wifi_sigma.cfg`, `txpowerctrl.cfg`);
  - the modules above, in `/vendor_dlkm/lib/modules/` (Android loads them from
    `/vendor/lib/modules/`);
  - the `nvdata` partition (`PARTNAME=nvdata`, `/dev/sdc6` on the project's unit). It holds
    `APCFG/APRDEB/WIFI` (Wi-Fi NVRAM: MAC, calibration) and `APCFG/APRDEB/BT_Addr`. It is
    only ever mounted read-only.
- The kernel must find `/vendor/firmware` through `request_firmware`. It worked on this boot
  chain, but the notes do not record where `firmware_class.path` is set. Check with
  `cat /sys/module/firmware_class/parameters/path`.
- To build (desktop): Android clang `r416183b` (the prebuilt used for the kernel build,
  [docs/02](02-kernel.md)), the Alpine `musl-dev-1.2.6-r3` aarch64 package (SHA-256 used:
  `45afe0a9f78de3402b6b5d11f5153ed955bf59d59b45344d9c3fed9f4bbf2f66`), the Alpine signing key
  `alpine-devel@lists.alpinelinux.org-616ae350.rsa.pub` (from the `alpine-keys` package),
  `openssl`, and optionally `qemu-aarch64-static` for the offline tests.
- For NetworkManager: Omarchy installed ([docs/04](04-omarchy.md)), which brings
  `networkmanager`, `bluez`, `bluez-utils`.

## Files in this repo

| Repo path | Installed to | Purpose |
|---|---|---|
| `connectivity/src/mtk-wmt-init.c` | built to `/opt/omarchy-tablet/mtk-wmt-init` | Loader + launcher daemon + `wifi-nvram` helper (v0.4) |
| `connectivity/src/mtk-bt-bridge.c` | built to `/opt/omarchy-tablet/mtk-bt-bridge` | `/dev/stpbt` <-> BlueZ H4 line discipline on a pty |
| `connectivity/build.sh` | desktop | Static aarch64/musl build (set `CLANG_DIR`) |
| `initramfs/tools/apkfetch.py` | desktop | Verifies an Alpine apk signature + datahash, extracts it |
| `connectivity/tools/annot.py` | desktop | Annotates `llvm-objdump` listings with strings/PLT names (used for the reverse engineering) |
| `connectivity/tests/*` | desktop | qemu-user test of the daemon loop (a pty stands in for `/dev/stpwmt`) |
| `connectivity/scripts/wifi-up.sh`, `wifi-up-reload.sh`, `wifi-down.sh` | `/opt/omarchy-tablet/` | Wi-Fi orchestration |
| `connectivity/scripts/bt-up.sh`, `bt-down.sh` | `/opt/omarchy-tablet/` | Bluetooth bridge start/stop |
| `rootfs/onboard/load-conn-nowifi.list` | `/opt/omarchy-tablet/` | Boot module list for connsys **without** `wmt_chrdev_wifi` / gen4m |
| `connectivity/networkmanager/setup-network.py` | run once as root | Moves an existing `wpa_supplicant-wlan0.conf` network to a NetworkManager profile |
| `connectivity/usb-gadget/omarchy-usb-gadget` | `/usr/local/sbin/` | configfs NCM/ECM gadget supervisor (rootfs copy of the initramfs script, `#!/bin/sh`) |
| `rootfs/overlay/etc/systemd/system/omarchy-usb-gadget.service`, `rootfs/overlay/etc/systemd/network/10-usb-gadget.network` | `/etc/systemd/system/`, `/etc/systemd/network/` | Tablet side of the USB link |
| `tools/host-usbnet.sh`, `tools/host/05-omarchy-tablet.network` | desktop | Desktop side of the USB link |

The boot-time caller of the Wi-Fi/BT scripts is the vendor hardware unit
(`omarchy-hw.service` -> `/opt/omarchy-tablet/hw-up.sh`, [docs/03](03-boot-and-rootfs.md)). The scripts carry a
`#!/omarchy/bin/busybox sh` shebang from the initramfs era. On the rootfs they are always
run as `sh script`.

## Reproduction steps

### 1. Build (desktop)

```sh
cd connectivity
curl -O https://dl-cdn.alpinelinux.org/alpine/latest-stable/main/aarch64/musl-dev-1.2.6-r3.apk   # may have moved to an archive
sha256sum musl-dev-1.2.6-r3.apk     # compare with the hash above
python3 -I tools/apkfetch.py musl-dev-1.2.6-r3.apk /path/to/alpine-devel@lists.alpinelinux.org-616ae350.rsa.pub work/musl-sysroot
CLANG_DIR=/path/to/clang-r416183b ./build.sh        # -> out/mtk-wmt-init, out/mtk-bt-bridge, out/SHA256SUMS
```

Offline checks (no device needed):

```sh
# dry run: prints the ioctl sequence and the firmware-file selection; needs a copy of your /vendor/firmware
qemu-aarch64-static out/mtk-wmt-init --dry-run -p <copy-of-vendor-firmware> --state /tmp/x --devdir /tmp/y
# daemon loop against a fake kernel
CLANG_DIR=/path/to/clang-r416183b sh tests/build-test.sh
python3 -I tests/daemon-test.py work/t_mtk <fwdir> /tmp/dt
```

`build.sh` and `tests/build-test.sh` were adapted for this repo (clang location via
`CLANG_DIR`). The adapted versions were not re-run. The original build produced
statically linked, stripped aarch64 binaries.

### 2. Install on the tablet

```sh
tar -C connectivity/out -cf - mtk-wmt-init mtk-bt-bridge | tools/tablet 'tar -C /opt/omarchy-tablet --no-same-owner -xf -'
tar -C connectivity/scripts -cf - . | tools/tablet 'tar -C /opt/omarchy-tablet --no-same-owner -xf -'
tools/tablet 'cp /dev/stdin /opt/omarchy-tablet/load-conn-nowifi.list' < rootfs/onboard/load-conn-nowifi.list
tools/tablet 'chmod 0755 /opt/omarchy-tablet/mtk-wmt-init /opt/omarchy-tablet/mtk-bt-bridge'
```

### 3. Module order (the critical part)

At boot, load the connsys modules **except** `wmt_chrdev_wifi` and
`wlan_drv_gen4m_6789` (`load-conn-nowifi.list`: ccci helpers, `btif_drv`, `connadp`,
`mddp`, `wmt_drv`, `bt_drv_connac1x`, `connfem`). `cfg80211` must also be loaded (it is
checked by `wifi-up.sh`). Then `wifi-up.sh`:

1. checks `/dev/wmtdetect`, the modules and `/vendor/firmware/WMT_SOC.cfg`;
2. mounts `nvdata` with `ro,noload,nodev,nosuid,noexec` after `blockdev --setro` at
   `/mnt/vendor/nvdata`;
3. runs `mtk-wmt-init`: the loader stage, then the launcher daemon, which stays resident;
   the parent exits 0 only after the power-on verdict;
4. **only now** `insmod`s `wmt_chrdev_wifi.ko` and `wlan_drv_gen4m_6789.ko` (if gen4m was
   already loaded before the loader stage, it is unloaded and reloaded: `RELOAD=auto`);
5. pushes `APCFG/APRDEB/WIFI` with `mtk-wmt-init wifi-nvram` if present;
6. writes `1` to `/dev/wmtWifi` and waits for `wlan0`.

The project's `hw-up.sh` does exactly this:

```sh
./load-modules.sh load-power.list load-gpu.list load-audio.list load-extra.list load-conn-nowifi.list
./load-modules.sh load-sensors.list
mkdir -p /run/mtk-wmt
BIN=$D/mtk-wmt-init MODDIR=/vendor_dlkm/lib/modules sh ./wifi-up.sh > /run/mtk-wmt/wifi-up.out 2>&1
# quieten the gen4m driver: level 0x03 for all 32 debug modules
i=0; while [ "$i" -lt 32 ]; do printf '0x%02x:0x03\n' "$i" > /proc/net/wlan/dbgLevel; i=$((i+1)); done
BIN=$D/mtk-bt-bridge sh ./bt-up.sh > /run/omarchy-hw/bt-up.out 2>&1
```

`omarchy-hw.service` uses `KillMode=process`, so the resident `mtk-wmt-init` and
`mtk-bt-bridge` daemons survive in its cgroup after the oneshot finishes. It is ordered
`Before=network-pre.target bluetooth.service wpa_supplicant@wlan0.service`.

For a manual first try over the USB link, run detached as recorded (connsys power-on has
glitched the USB gadget on other MediaTek ports):

```sh
tools/tablet 'mkdir -p /run/mtk-wmt; cd /opt/omarchy-tablet && setsid sh ./wifi-up.sh > /run/mtk-wmt/wifi-up.out 2>&1 &'
tools/tablet 'cat /run/mtk-wmt/wifi-up.out; cat /run/mtk-wmt/mtk-wmt-init.log | tail -n 25'
```

Expected kernel log, in order: `[WMT-DETECT]` ioctl lines and `WMT-CONSYS-HW`
clock/regulator lines; `WMT_SOC.cfg` read, `mtk_wmtd` threads, `/dev/stpwmt` present;
`luncher set STP mode success!` (sic), `chipid = 0x6789`; per-image
`[Rom Patch]Name=soc2_2_ram_{mcu,bt,wifi}_1d_1_hdr.bin,EmiOffset=...`; mtk-wmt-init logs
`cmd(srh_rom_patch) resp(ok)` and `power on connsys: SUCCESS`; then
`[MTK-WIFI] WIFI_write`, `enter wlanProbe`, `MAC address: xx:xx:**:**:**:xx`,
`WMT turn on WIFI success!`. `wlan0` shows in `ip link`. `wifi-up.sh` exit codes: 0 ok,
1 prerequisites, 2 mtk-wmt-init failed, 3 `/dev/wmtWifi` write failed, 4 no `wlan0`.

### 4. Associate: from wpa_supplicant to NetworkManager

The pre-Omarchy prototype used `wpa_supplicant@wlan0` (config
`/etc/wpa_supplicant/wpa_supplicant-wlan0.conf`) with systemd-networkd. `iwd` was tried
and fails for lack of AF_ALG. How the original wpa_supplicant config was written is not
recorded. Any config with exactly **one** `network={}` block containing `ssid` and `psk`
works with the migration script (for example `wpa_passphrase` output).

With Omarchy installed, NetworkManager owns `wlan0`:

```sh
tools/tablet 'python3 -' < connectivity/networkmanager/setup-network.py
```

It reads the single saved network locally (it never prints or sends the PSK) and writes:

- `/etc/NetworkManager/system-connections/tablet-wifi.nmconnection` (0600):
  `key-mgmt=wpa-psk`, `proto=rsn`, `auth-alg=open`, **`pmf=1` (disable)**,
  `cloned-mac-address=permanent`, `interface-name=wlan0`, DHCP for IPv4 and IPv6;
- `/etc/NetworkManager/conf.d/20-tablet.conf`: `dns=systemd-resolved` and
  `unmanaged-devices` for `usb0`, `wlan1`, `p2p0`, `ap0`, `dummy0`, `ifb0`, `ifb1`. This
  keeps NetworkManager away from the USB debug link and the vendor virtual interfaces;
- `/etc/systemd/network/05-wlan0-unmanaged.network`: networkd ignores `wlan0` (networkd
  still runs the USB link).

Then the per-interface supplicant was disabled and NetworkManager used (the notes record the
outcome; these commands are reconstructed):

```sh
tools/tablet 'systemctl disable --now wpa_supplicant@wlan0.service; systemctl restart systemd-networkd; systemctl enable --now NetworkManager.service'
tools/tablet 'nmcli -f GENERAL.STATE,IP4.ADDRESS,IP4.GATEWAY device show wlan0'
```

Recorded result: NetworkManager connected with a DHCP address on the home network, and the
connection returned after every later reboot.

### 5. Bluetooth

`bt-up.sh` needs the `mtk-wmt-init` daemon (it starts one if `launcher.ready` is missing),
`bt_drv_connac1x` loaded and `/dev/stpbt`. It starts `mtk-bt-bridge` (with
`--bdaddr /mnt/vendor/nvdata/APCFG/APRDEB/BT_Addr` if that file exists) and waits up to
30 s for `/sys/class/bluetooth/hci0`. Then start `bluetooth.service`.

Verify:

```sh
tools/tablet 'dmesg | grep -E "BT_open|MTK Bluetooth Mode"; ls /sys/class/bluetooth; tail /run/mtk-wmt/mtk-bt-bridge.log'
tools/tablet 'bluetoothctl show'
```

Only the first line has been observed in the notes (boot log: `BT_open: WMT turn on BT OK!`).
`hci0`, `bluetoothctl` and pairing are not recorded as tested.

### 6. USB debug link (CDC-NCM, ECM fallback)

The gadget is created in the initramfs ([docs/03](03-boot-and-rootfs.md)) and adopted on the rootfs by
`omarchy-usb-gadget watch`. That script keeps it bound to `musb-hdrc`, applies MediaTek musb
quirks (`kernel_init_done`, `cmode`, and `musb_force_on` once after 15 s without a host
session), and swaps NCM <-> ECM if the host enumerates but never raises carrier. Fixed,
locally administered MACs: NCM tablet `02:a1:b2:c3:d4:01` / host `...:02`, ECM tablet `...:11`
/ host `...:12`. VID:PID `1d6b:0104`.

Tablet side (the boot-chain rootfs build installs these; listed here for completeness):

```sh
install -m 0755 omarchy-usb-gadget /usr/local/sbin/
install -m 0644 omarchy-usb-gadget.service /etc/systemd/system/ && systemctl enable omarchy-usb-gadget.service
install -m 0644 10-usb-gadget.network /etc/systemd/network/      # 172.16.42.1/24, gateway/DNS 172.16.42.2
```

Desktop side, either NetworkManager (automatic, unprivileged) or networkd (one-time sudo):

```sh
tools/host-usbnet.sh --wait     # finds the interface by MAC, configures 172.16.42.2/24
tools/host-usbnet.sh --install  # prints the networkd install commands
ping -c1 172.16.42.1 && tools/tablet true
```

The commented `IPMasquerade=ipv4` / `IPv4Forwarding=yes` lines in `05-omarchy-tablet.network`
let the desktop share its internet with the tablet over the link. A desktop firewall that
blocks inbound traffic does not affect outbound SSH to the tablet. For tablet-to-desktop
traffic, the project used `ssh -R` tunnels.

Power over this link: a desktop USB port is an SDP with 500 mA. The board's charger profile
asked for 1.2 A, and the tablet drains under load. The 500 mA SDP ceiling service is in
[docs/11](11-power-battery.md).

## Pitfalls and lessons

- **The USB debug link can stay dead after the desktop reboots (2026-11-03).** The
  tablet enumerated once while the host booted, then dropped. After that, every
  attempt failed with `usb 1-3: device descriptor read/64, error -110`, and no
  network interface appeared on the host. Rebooting the tablet fixed it: the
  gadget came back with the next boot. `omarchy-usb-gadget watch` does not catch
  this case yet.
- **EMI base is latched at module init (the main Wi-Fi bug).** `wlan_drv_gen4m_6789`'s
  `init_module` copies `gConEmiPhyBase`/`gConEmiSize` from `wmt_drv` into
  `gConEmiPhyBaseFinal` once. `wmt_drv` only fills those variables in `mtk_wmt_probe`. That
  platform driver is registered by the loader's `CONNSYS_SOC_HW_INIT` ioctl, not by
  `insmod wmt_drv`. When gen4m was loaded first (try 2), the ROM-patch download still worked,
  but `wlanProbe` failed with
  `wlanDownloadEMISection: Consys emi memory address gConEmiPhyBaseFinal invalid` ->
  `probe failed, reason:3` -> `WMT turn on WIFI fail!`. No userspace write can fix the
  latched value. Load gen4m after the loader stage, or rmmod and insmod again.
  `mtk-wmt-init` 0.4 warns and writes `wlan.stale` if it finds gen4m loaded too early. Only
  gen4m imports `gConEmi*`. `wmt_chrdev_wifi` is reloaded with it only to keep Android's order.
- **`echo 1 > /dev/wmtWifi` before WMT init** fails with `WMT turn on WIFI fail` /
  "Queue is NULL, name: wmtd_thread". The queues and threads only exist after the
  loader's `DO_MODULE_INIT`.
- **The launcher daemon must stay alive.** Closing the last fd of `/dev/stpwmt` tells the
  kernel the launcher is gone, and it can then no longer answer patch requests (for example on
  chip reset). Do not kill it with `wifi-down.sh KILL=1` unless you mean it. Keep
  `KillMode=process` on the unit that starts it.
- **`/vendor/firmware` must be visible before the loader runs.** Otherwise:
  `read wmt config file fail` / `empty Wmtcfg name`.
- **Never write to `nvdata`.** It holds factory calibration. Android's `nvram_daemon` is its
  only writer, and this project does not run it. The scripts mount it `ro,noload` (no journal
  replay) after `blockdev --setro`.
- **SAE/PMF**: NetworkManager's automatic SAE/PMF negotiation failed during association with
  this vendor driver. The fix was a WPA2-only profile (`proto=rsn`, `pmf=1`). An AP set to
  WPA3-only will not work.
- **No iwd.** iwd needs AF_ALG. Enabling `CONFIG_CRYPTO_USER_API_*` would mean another kernel
  config change, which was not attempted. Omarchy 4 itself uses NetworkManager, so this does
  not matter for the final setup.
- **Keep NetworkManager off `usb0`**, or you lose the debug link the moment NM touches it.
- `Direct firmware load for wifi.cfg / wifi_sigma.cfg failed -2` is expected (optional files).
- The gen4m driver logs a lot. `hw-up.sh` lowers `/proc/net/wlan/dbgLevel`.
- **Display-blank coupling (unverified):** `wmt_dev` registers a display-blank notifier.
  With Wi-Fi and BT off, connsys is power-cycled with the screen. With either on, it stays
  powered.
- **No `CONFIG_BT_HCIVHCI`** in the vendor kernel, so there is no vhci. The bridge attaches the
  `N_HCI` line discipline to a pty (what `btattach -P h4` does) and copies H4 packets. Only one
  process may open `/dev/stpbt` at a time ("BT already on" -> `-EIO`).
- `autobt` on the vendor partition is MediaTek's RF certification tool, not part of normal
  BT operation.

## Rollback

- Wi-Fi off: `sh /opt/omarchy-tablet/wifi-down.sh` (`wlan0` down, `0` to `/dev/wmtWifi`).
  `KILL=1` also stops the daemon. `NVUMOUNT=1` unmounts `nvdata` if `wifi-up.sh` mounted it.
- BT off: `sh /opt/omarchy-tablet/bt-down.sh`.
- Back to wpa_supplicant + networkd: remove `/etc/systemd/network/05-wlan0-unmanaged.network`
  and the NM profile/conf, then disable NetworkManager and re-enable `wpa_supplicant@wlan0`.
  The original supplicant config is not deleted by `setup-network.py`.
- To disable everything at boot, drop the `wifi-up.sh` / `bt-up.sh` lines from the hardware
  unit ([docs/03](03-boot-and-rootfs.md)). Nothing here writes to flash or partitions.

## How it works (reverse-engineering summary)

Sources: disassembly of the device's `/vendor/bin/wmt_loader`, `/vendor/bin/wmt_launcher`
and `/vendor/bin/wlan_assistant` (tags `[L:]`, `[W:]`, `[A:]` with addresses in the source
comments); strings/disassembly of the device's `.ko` files; MediaTek's MT6789 connectivity
module source (`[K:]`, from https://github.com/Gigaset-dev/android_kernel_gigaset_mt6789-extras);
and other ports that re-implemented the same flow (read, not copied):
https://github.com/Muzuwi/wmt-pyloader (MT6765), Sabslgs/galaxy-a31-postmarketos `connsys/`
(MT6768), Barley-Project/tb330fu-ubuntu (MT6768, Python), https://gitlab.com/Dahrkael/openmttools.
To repeat the analysis, pull those three binaries and the modules from your own `/vendor`
and `/vendor_dlkm`, then run:

```sh
llvm-objcopy --dump-section .gnu_debugdata=x.xz wmt_launcher /dev/null; xz -dc x.xz > x.dbg; llvm-nm -n x.dbg
llvm-objdump -d --no-show-raw-insn wmt_launcher > w.dis; python3 -I connectivity/tools/annot.py wmt_launcher w.dis > wmt_launcher.ann
llvm-objdump -dr wmt_drv.ko      # stores to gConEmiPhyBase: only in mtk_wmt_probe
```

Android's sequence: `insmod connfem, wmt_drv` -> `wmt_loader` -> `wmt_launcher -p /vendor/firmware/ -o 1`
(resident) -> on `vendor.connsys.driver.ready=yes`: `insmod wmt_chrdev_wifi, wlan_drv_gen4m_6789, bt_drv_connac1x`,
start `wlan_assistant` -> the Wi-Fi HAL writes to `/dev/wmtWifi`, and the BT HAL opens `/dev/stpbt`.

### Stage 1, loader (`/dev/wmtdetect`, magic `'w'`, scalar args, result = ioctl return)

| ioctl | number | effect |
|---|---|---|
| CONNSYS_SOC_HW_INIT | `0x8004770a` | SoC connsys clocks/regulators; registers the platform driver whose probe sets the EMI base. Must return 0 |
| EXT_CHIP_PWR_ON | `0x80047706` | Returns -1 on this SoC-only build (expected) |
| GET_SOC_CHIP_ID | `0x80047703` | `0x6789` (retried up to 20 x 500 ms) |
| SET_CHIP_ID | `0x40047701` | Stores the id |
| MODULE_CLEANUP | `0x80047705` | Must be 0 |
| DO_MODULE_INIT | `0x80047704` | `WMT_init()`: creates `/dev/stpwmt` (class `stpwmt`), reads `WMT_SOC.cfg` via `request_firmware`, starts `mtk_wmtd` threads |
| GET_ADIE_CHIP_ID | `0x80047709` | A-die id (`0x6631` expected; inferred, the code stops with a clear error otherwise) |

`mtk-wmt-init` creates missing `/dev` nodes from `/sys/class/*/<name>/dev` for the four
known names only. It does not use `O_CREAT` on device paths.

### Stage 2, launcher (`/dev/stpwmt`, magic `0xa0`)

1. `QUERY_CHIPID 0x8004a016` until it is not -1;
2. `SET_STP_MODE 0x4004a005` with `0x23` (BTIF full STP mode; mandatory, power-on branches on HIF type);
3. `SET_LAUNCHER_KILL 0x4004a00d` with 0;
4. thread: `LPBK_POWER_CTRL 0x4004a007` with 2 (`-o 1`), up to 20 tries. This is the full
   connsys power-on including ROM-patch download into EMI;
5. main loop: `poll`/`read` command strings, answer with `write("ok")` (the kernel accepts
   only literal `ok`), `cmd not found` or `resp_<n>`. The kernel waits 6 s per command.

`srh_rom_patch` (the only command that matters for `soc2_2`): prefix `soc2_2_ram` for chip
`0x6789`, pattern `1d_1` for A-die `0x6631` (`1e_1` for `0x6635`). For each matching file in
`/vendor/firmware`: header byte `0x17` must equal `fw_version & 0xff` (`GET_CHIP_INFO 0x8004a00c`
arg 2); `info[8]` at offset `0x18`; type = `info[7]` (BT 0, Wi-Fi 3, MCU 4); EMI address bytes
`{0, info[1], info[2], info[3]}` (**low byte zeroed**); sent as a 264-byte struct
`{u32 type; u8 addr[4]; char name[256]}` with `SET_ROM_PATCH_INFO 0x4008a01f`. The kernel
loads the file contents itself. `mtk-wmt-init` answers `resp_-6` if nothing matched, so the
failure is visible (the vendor binary answers success). Options for problems:
`--no-vercheck`, `--pattern 1d_1|1e_1`, `--power-arg 1`, `--no-poweron`.

### `/dev/wmtWifi` commands (first byte matters)

| Write | Effect |
|---|---|
| `1` | Wi-Fi function on -> gen4m `wlanProbe` -> `wlan0` |
| `0` | Wi-Fi off (`wlan0` removed) |
| `P` / `S` | On + STA/P2P (`p2p0`); what the Android HAL writes |
| `A` | On + AP mode (`ap0`) |
| `WR-BUF:NVRAM<data>` | NVRAM push |

### NVRAM (`wlan_assistant` equivalent)

File `/mnt/vendor/nvdata/APCFG/APRDEB/WIFI`; payload = file size - 2, which must be a
positive multiple of 256; one `write()` of `"WR-BUF:NVRAM"` + file bytes. gen4m stores it
and uses MAC, country code, 5 GHz and regulatory data at probe. Without it the driver logs
`Nvram not available` and falls back to defaults/random MAC. Nothing is written back.

### Bluetooth

`bt_drv_connac1x` registers `/dev/stpbt`. `open()` turns the BT function on (needs the
launcher alive; BT RAM code went to EMI with `srh_rom_patch` type 0). `read`/`write` carry
raw H4 packets. `close()` turns BT off. `mtk-bt-bridge` opens it, creates a pty, sets
`TIOCSETD N_HCI` and `HCIUARTSETPROTO H4` on the slave, and copies complete H4 packets both
ways. Its quirk switches (`--no-synctrain-fix`, `--bdaddr FILE`, `--no-le-fix`) come from
the MT6768 port and are unverified here.

## Licensing

`mtk-wmt-init.c` and `mtk-bt-bridge.c` are marked GPL-2.0 (ioctl numbers and structures
derived from GPL kernel headers). Reference sources are linked above, not included.
