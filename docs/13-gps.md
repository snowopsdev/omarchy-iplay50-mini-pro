# 13 - GPS (vendor mnld in a Bionic chroot, bridged to gpsd)

The tablet's GNSS receiver lives in the MT6631 connectivity chip, next to
Wi-Fi and Bluetooth. The positioning engine is MediaTek's closed userspace
daemon `mnld` (`/vendor/bin/mnld`, 64-bit Bionic). This project runs that
daemon unchanged in a private chroot, answers the few Android services it
expects, and talks to it with the same socket protocol as the vendor HAL
module `gps.default.so`. NMEA comes out on a pseudo-terminal that `gpsd`
reads, so any Linux GPS client works. Work date: 2026-10-30. Files:
[`gps/`](../gps/README.md).

## Status

| Item | State | Evidence |
|---|---|---|
| Kernel side: `gps_pwr.ko`, `gps_drv_stp.ko` (stock vendor modules) | Works | Load cleanly once connsys is up (Wi-Fi running); create `/dev/stpgps`, `/dev/gps_pwr`, `/dev/gps_emi` |
| `mnld` starts and initialises the receiver | Works | Log: chip `0x6789`, A-die `0x6631`, 6 constellations, `GPS2MAIN_EVENT_START_DONE` |
| Start / stop / time injection over the HAL socket | Works | `mnld` logs `hal_gps_inject_time ... uncertainty=1000` with the correct UTC time; stop is acted on |
| NMEA stream to `gpsd` (`/run/tablet-gps/nmea`) | Works | `gpspipe -w` shows the device with driver `NMEA0183` and TPV reports once per second |
| **Position fix** | **Not verified** | All tests ran indoors; no satellites were seen (`mode 1`, no GSV). Needs a test with sky view |
| Assisted GPS (EPO/QEPO downloads, SUPL, modem time) | Not working | `mnld` tries and fails (no `agpsd`, no data directories, no modem link); expect slow cold starts |
| GeoClue / browser geolocation | Not set up | Only `gpsd` is wired |
| Power use, long runs, suspend | Unverified | The engine runs only while `tablet-gps.service` runs; it is not enabled at boot |

## Prerequisites

- Wi-Fi/Bluetooth bring-up from [docs/05](05-wifi-bluetooth.md) (connsys
  powered on; `wmt_drv` loaded). The GPS modules depend on it.
- The video runtime from [docs/09](09-video-decoding.md), step for the
  read-only Android `system` image and runtime APEX:
  `/opt/omarchy-tablet/video/android-system` and
  `/opt/omarchy-tablet/video/runtime-apex/mnt` must be mounted (the VCU
  service does this at boot). The GPS chroot reuses both read-only.
- That `system` image must contain `system_ext/apex/com.android.vndk.v31.apex`
  (the project's GSI-based system had VNDK v28-v34). The vendor daemon is
  API 31 and needs the v31 libraries.
- Tablet packages: `gpsd` (3.27.5 was used), `python`.
- Desktop: Android clang `r416183b` (the kernel toolchain, [docs/02](02-kernel.md))
  to build the property shim.
- From your own tablet, read in place (never copied into this repo):
  `/vendor/bin/mnld`, its `/vendor/lib64` libraries, `/vendor/etc/MNL_Config.xml`,
  `/vendor_dlkm/lib/modules/gps_{pwr,drv_stp}.ko`, and `/mnt/vendor/nvdata`
  (bound read-only into the chroot).

## Reproduction

1. **Build the property shim (desktop):**

   ```sh
   CLANG_DIR=/path/to/clang-r416183b gps/build-gpsprops.sh
   # 9992efaacc0c75310ef582fb38e0fcb0e26fe50c86daaa55d3ac4b8cad9a33dd on the project's build
   ```

2. **Install the files (tablet, root):**

   ```sh
   g=/opt/omarchy-tablet/gps
   install -d $g/shim/lib64 $g/vndk-v31/mnt
   install -m0644 gps/out/libgpsprops.so $g/shim/lib64/
   install -m0644 gps/ld.config.txt $g/
   install -m0755 gps/gps-runtime-inner.sh gps/logd-sink.py gps/tablet-gnss-bridge.py gps/mnl-probe.py $g/
   install -m0644 gps/tablet-gps-mnld.service gps/tablet-gps.service /etc/systemd/system/
   ```

3. **Extract the VNDK v31 payload (tablet, root)** from the mounted system image:

   ```sh
   python3 -c 'import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extract("apex_payload.img", sys.argv[2])' \
     /opt/omarchy-tablet/video/android-system/system/system_ext/apex/com.android.vndk.v31.apex \
     /opt/omarchy-tablet/gps/vndk-v31
   ```

   (about 42 MB; the unit loop-mounts it read-only.)

4. **gpsd with socket activation, then start GPS:**

   ```sh
   systemctl daemon-reload
   systemctl enable --now gpsd.socket
   systemctl start tablet-gps          # pulls in tablet-gps-mnld
   ```

5. **Verify:**

   ```sh
   systemctl is-active tablet-gps tablet-gps-mnld     # active active
   ls -lL /run/tablet-gps/nmea                        # crw-rw---- root uucp (a pty)
   gpspipe -w -n 8                                    # DEVICE driver "NMEA0183", TPV every second
   grep -E "START_DONE|inject_time" /run/omarchy-tablet-gps/mnld.log
   ```

   With sky view, `cgps` or `gpsmon` should show satellites (GSV) within a
   few minutes and then a fix (TPV `mode` 2 or 3). That last step was not
   observed on the project's tablet (indoors).

6. **Stop when done** (saves power): `systemctl stop tablet-gps`. The daemon
   unit stops with it (`StopWhenUnneeded`).

## How it works

### The chroot

`gps-runtime-inner.sh` runs inside `unshare -m -p -f --mount-proc`, so nothing
it mounts is visible to the host. It binds read-only: the Android `system`
image at `/system`, the vendor partition at `/vendor`, the runtime APEX at
`/apex/com.android.runtime`, the VNDK v31 payload at `/apex/com.android.vndk.v31`,
`nvdata`, and the shim. `/dev` is a tmpfs with only the GPS nodes and
`null/zero/random/urandom`; `/data` and `/tmp` are tmpfs. Then it chroots and
execs `mnld` with `LD_PRELOAD=/gps-shim/lib64/libgpsprops.so`. (The host
`chroot` binary prints a harmless "cannot be preloaded" warning for that
variable; only `mnld` needs it.)

### Three link-time problems and their fixes

1. `libnvram.so` needs `android::base::Basename(std::string const&)`, which the
   newer (API 36) `/system/lib64/libbase.so` no longer has. Fix: put the VNDK
   v31 APEX in front, as Android does for vendor code.
2. With everything in one namespace, the system `liblog.so` then failed on a
   libc++ symbol missing from the older v31 `libc++.so`. Fix: two namespaces
   like Android's own linker config. `default` holds vendor + VNDK v31;
   `system` holds Bionic and the newer system libraries; only LLNDK libraries
   (`libc`, `libm`, `libdl`, `liblog`, ...) are shared across the link.
   See `gps/ld.config.txt`.
3. No property service: Bionic finds no `/dev/__properties__`, so every
   property read is empty, and `mnld` cannot detect the chip ("get chip id
   fail, retry", then "connsys driver not ready"). Fix: `libgpsprops.so`
   interposes `__system_property_get/find/read/read_callback/set/serial/foreach`
   with an in-memory table. Defaults are what MediaTek's connsys tools set
   on Android: `persist.vendor.connsys.chipid=0x6789`,
   `vendor.connsys.adie.chipid=0x6631`, `vendor.connsys.driver.ready=yes`,
   `vendor.service.nvram_init=Ready`. `/gps-shim/props.txt` (`name=value` lines)
   overrides them. Misses and sets are printed to the daemon's stderr (the
   journal).

`mnld` logs through `liblog` to `/dev/socket/logdw`; `logd-sink.py` listens
there and writes text lines to `/run/omarchy-tablet-gps/mnld.log`.

### The HAL protocol

Reconstructed from the exported `hal2mnl_*`/`put_*` functions of the vendor
module `gps.default.so` (disassembly only; the module is not used):

- Transport: abstract AF_UNIX **datagram** sockets. Commands go to
  `mtk_hal2mnl`; `mnld` sends to `mtk_mnl2hal`, which the client binds. The
  name is copied into `sun_path+1` and the full 110-byte address is used, so
  the abstract name is zero-padded to 108 bytes.
- Message: little-endian `int32` interface version (`1`), `int32` command,
  then arguments packed with no padding.
- Commands used: `101` init; `102` start, followed by `float32` 1.4 (the
  value the HAL sends); `103` stop; `105` inject time (`int64` UTC ms,
  `int64` boot-clock ms, `int32` uncertainty ms). Others exist
  (`hal2mnl_gps_inject_location`, `..._set_position_mode`, AGPS data
  connection calls) and are not used.
- Replies: type `104` carries one NMEA sentence; others seen right after
  start are `201`, `308` (daemon/firmware version string), `103`, `102`, `309`.

`tablet-gnss-bridge.py` sends init, the current time and start, writes each
sentence (CRLF) to a pty, links it as `/run/tablet-gps/nmea` (group `uucp`,
mode 0660, since `gpsd` runs as `nobody:uucp`), re-sends start if NMEA stops
for 15 s (for example after a daemon restart), and sends stop on SIGTERM.
`tablet-gps.service` registers the pty with `gpsdctl add`.

## Pitfalls

- **gpsd exits when its last device is removed** if it was launched by
  `gpsdctl`; later clients then get "connection refused". Use `gpsd.socket`.
- **`gpsdctl add` fails when gpsd already knows the device**; the unit
  ignores that error (`ExecStartPost=-...`).
- **`StopWhenUnneeded`** on the daemon unit means `systemctl start
  tablet-gps-mnld` alone starts it and then immediately stops it. Start
  `tablet-gps`.
- The daemon expects many more services (`agpsd`, a debug service, an SCP
  sensor-hub channel `/dev/gps2scp`, an alarm timer). Their absence is logged
  as errors and is harmless for basic GNSS.

## Rollback

```sh
systemctl stop tablet-gps; systemctl disable --now gpsd.socket
rm /etc/systemd/system/tablet-gps.service /etc/systemd/system/tablet-gps-mnld.service
umount /opt/omarchy-tablet/gps/vndk-v31/mnt; rm -r /opt/omarchy-tablet/gps
rmmod gps_drv_stp gps_pwr
systemctl daemon-reload
```

Nothing outside `/opt/omarchy-tablet/gps`, the two units and `gpsd.socket` is
changed; no partition, kernel or vendor file is written.
