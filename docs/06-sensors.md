# 06 - Sensors: auto-rotation and touch alignment

The tablet's accelerometer is a Silan SC7A20 (`sc7a20_acc`) behind MediaTek's "HF
manager" sensor framework (`/dev/hf_manager`, vendor modules `hf_manager.ko` +
`sensorhub.ko`). It is not an IIO device, so `iio-sensor-proxy` cannot read it. The two
IIO devices that do exist are PMIC/SoC ADCs. A small Python watcher,
`tablet-orientation`, reads only accelerometer events and rotates the Hyprland output
and the touchscreen together.

## Status

| Item | State |
|---|---|
| Accelerometer via `/dev/hf_manager` (sensor type 1, `sc7a20_acc`, vendor `silan`, gain 1000) | Works |
| Gyroscope (sensor type 4) | **Not registered** on this unit |
| Auto-rotation, all four orientations, correct direction | Works. User physically confirmed "Picture and touch are correct" |
| Touch alignment after rotation | Works (same confirmation; injected touch also typed `chr` in landscape and portrait menus) |
| Rotation persists across desktop restarts and reboots | Works (sensor modules, live readings with Auto on after the 18:11 reboot) |
| Charger-only boot | Sensor modules intentionally not loaded |
| Time to settle after a rotation | About 0.7-1.0 s, mostly apps re-rendering in software |
| Option A touch shell in portrait | Unverified at the time of the notes ([docs/10](10-touch-ui.md)) |
| Light/proximity/other sensors | Not investigated |

## Prerequisites

- Vendor modules from your own `vendor_dlkm`: `hf_manager.ko`, `sensorhub.ko`
  (`rootfs/onboard/load-sensors.list`). They load against the existing kernel. Nothing is flashed.
- The desktop from [docs/04](04-omarchy.md): `monitors.lua` (reads the saved transform)
  and `autostart.lua` (starts the watcher). `hypr` must be in the `input` group (the desktop
  unit grants it through `SupplementaryGroups`).
- On the tablet: `python`, `hyprctl`, `notify-send` (all present with Omarchy).

## Files in this repo

| Repo path | Installed to | Purpose |
|---|---|---|
| `sensors/tablet-orientation` | `/opt/omarchy-tablet/tablet-orientation` (0755) | Watcher and CLI |
| `sensors/99-tablet-sensors.rules` | `/etc/udev/rules.d/99-tablet-sensors.rules` | `/dev/hf_manager` -> group `input`, mode 0660 |
| `rootfs/onboard/load-sensors.list` | `/opt/omarchy-tablet/load-sensors.list` | Module list loaded by the hardware unit (normal boot only) |
| `omarchy/staged/monitors.lua` | `~hypr/.config/hypr/monitors.lua` | Applies the saved transform to DSI-1 and the touch device at startup |
| `sensors/landscape-touch.png` | - | Landscape after rotation: native menu search typed by touch on the on-screen keyboard (2026-10-28 17:44) |

## Reproduction steps

1. **Load the modules.** The hardware unit's `hw-up.sh` runs
   `./load-modules.sh load-sensors.list` after the other lists ([docs/03](03-boot-and-rootfs.md)). By hand:

   ```sh
   tools/tablet 'insmod /vendor_dlkm/lib/modules/hf_manager.ko; insmod /vendor_dlkm/lib/modules/sensorhub.ko; ls -l /dev/hf_manager'
   ```

2. **Device permissions.**

   ```sh
   tools/tablet 'cat > /etc/udev/rules.d/99-tablet-sensors.rules' < sensors/99-tablet-sensors.rules
   tools/tablet 'udevadm control --reload && udevadm trigger --name-match=hf_manager; ls -l /dev/hf_manager'
   ```

   Expected: `crw-rw---- root input ... /dev/hf_manager`. The reload/trigger line is
   reconstructed. The notes record only the rule and the 0660/input result.

3. **Install the watcher.**

   ```sh
   tools/tablet 'cat > /opt/omarchy-tablet/tablet-orientation && chmod 0755 /opt/omarchy-tablet/tablet-orientation' < sensors/tablet-orientation
   ```

   `autostart.lua` already contains `hl.exec_cmd("/opt/omarchy-tablet/tablet-orientation watch")`.
   Restart the desktop or start the watcher once by hand in the session.

4. **Turn auto-rotation on and verify.** As `hypr`, inside the session:

   ```sh
   /opt/omarchy-tablet/tablet-orientation auto      # toggles; notification "Automatic rotation on"
   /opt/omarchy-tablet/tablet-orientation status    # JSON from /run/user/1001/tablet-orientation.json
   ```

   The status shows `sensor`, the last `xyz` vector, `accuracy`, the `candidate` transform,
   the current `transform` and `auto`. Upright portrait read about `[909, 9747, 2654]`.
   Rotate the tablet and hold it for about a second. `hyprctl monitors` should show the new
   transform, and taps should land under the finger.

Other commands: `portrait`, `landscape`, `toggle` (switch portrait/landscape; these three
turn Auto off), `watch`. In the first Omarchy layout ([docs/04](04-omarchy.md)), the bar had
Rotate (= `toggle`) and Auto buttons. The later touch UI moved rotation/Auto into the
keyboard's Desktop drawer (`tablet-ui rotate|auto`, [docs/10](10-touch-ui.md)).

## Monitor transform, scale and touch mapping

The panel is a 1200x1920 portrait DSI panel driven at its native mode `1200x1920@60`,
scale 1.5. Every rotation sends one `hyprctl eval` with both settings:

```lua
hl.monitor({output="DSI-1", mode="1200x1920@60", position="0x0", scale=1.5, transform=T})
hl.config({input={touchdevice={output="DSI-1", transform=T}}})
```

| Accelerometer (Android axes: +X right, +Y top, +Z out of screen) | Physical pose | Transform `T` | Logical size |
|---|---|---|---|
| +Y dominant | Upright portrait | 0 | 800x1280 |
| -X dominant | Landscape | 1 | 1280x800 |
| -Y dominant | Upside-down portrait | 2 | 800x1280 |
| +X dominant | Landscape (other way) | 3 | 1280x800 |

The content rotates against the physical rotation. The first landscape mapping had +X and
-X swapped. Calibration while holding the tablet fixed it to +X -> 3, -X -> 1.

For transform 1 (landscape, scale 1.5), a logical point (x, y) maps to panel pixels
`px = 1.5*y`, `py = 1920 - 1.5*x`. The touch-injection tests in [docs/10](10-touch-ui.md) rely on this.

Keyboard and menu geometry follow the orientation: the on-screen keyboard is 360 logical
px tall in portrait and 260 in landscape, and the adapted Omarchy menu reserves the same
space ([docs/04](04-omarchy.md), `prepare-runtime.py`).

The chosen transform is written to `~/.local/state/omarchy/tablet-transform`.
`monitors.lua` reads it at compositor start, so the last orientation survives restarts.
Auto mode is the presence of `~/.local/state/omarchy/tablet-autorotate`.

## How the watcher works

- Opens `/dev/hf_manager` non-blocking and writes one packed 52-byte command
  (`struct hf_manager_cmd`): sensor type 1, action `HF_MANAGER_SENSOR_ENABLE`, length 16,
  data = batch `{delay = 200 ms, latency = 0}`. On exit it sends the same command with
  action `DISABLE`, so the sensor is off whenever the watcher is not running.
- Reads 76-byte `struct hf_manager_event` records (`<qBBBxiii`: timestamp, sensor type,
  accuracy, action, x, y, z) and keeps only the newest accelerometer `DATA_ACTION` sample in
  each read. Orientation needs only the current sample.
- Candidate: ignore the reading if the larger of |x|, |y| is below 6000 or below 1.2 x |z|
  (lying flat). A side needs to dominate by 1.35x, otherwise the reading counts as diagonal
  and is ignored.
- The candidate must stay the same for 0.8 s before it is applied. Status JSON is written at
  most every 0.5 s.
- A runtime `flock` (`$XDG_RUNTIME_DIR/tablet-orientation.lock`) makes sure only one watcher
  runs.
- No calibration, self-test or custom sensor commands are ever sent.

The struct layouts come from MediaTek's GPL sensor 2.0 headers in the Volla MT6789
kernel (`hf_sensor_io.h`, `hf_manager.h`):
https://github.com/HelloVolla/android_kernel_volla_mt6789/tree/volla-12.1/drivers/misc/mediatek/sensor/2.0
The live sensor-info ioctl (68-byte packet) and event reads confirmed them on the tablet.
The headers are not included here.

## Pitfalls and lessons

- **Rotate display and touch together.** One `hyprctl eval` sets both. Rotating only the
  output leaves touches landing in the old orientation.
- `hyprctl eval` can report Lua errors on stdout while exiting 0. The watcher checks stdout
  for `error`.
- Direction is easy to get backwards. Calibrate with the tablet physically held upright,
  not from reasoning alone.
- An IPC "prepare to rotate" hint from the watcher to the shell gained only about 0.15 s and
  was removed. `tablet-orientation` is back to its original version. The stale
  handle/dock scaling after rotation was fixed inside the touch shell instead
  ([docs/10](10-touch-ui.md)).
- `iio-sensor-proxy` will not find this sensor. Do not install it expecting rotation.
- Do not load the sensor modules in charger mode (the hardware unit does not).

## Rollback

- Fixed orientation: `tablet-orientation portrait` (Auto off), or delete
  `~/.local/state/omarchy/tablet-autorotate`.
- Remove the watcher line from `~/.config/hypr/autostart.lua` and delete
  `~/.local/state/omarchy/tablet-transform` to return to transform 0 at the next start.
- The root-only backup taken before the performance/rotation work is
  `/opt/omarchy-tablet/backups/pre-performance-rotation/`.

## Licensing

`tablet-orientation` is original to this project. Its packet layouts are derived from the
GPL-2.0 MediaTek headers linked above.
