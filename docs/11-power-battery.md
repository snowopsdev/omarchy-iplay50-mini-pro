# 11 - Power, charging and battery

What the battery gauge, charger and sleep paths do on this tablet under the stock
vendor GKI 5.10.149 kernel and vendor modules, the small helpers that were installed,
and what is still open. Work dates 2026-10-28..30.

Files in this repo:

| Path | What |
|---|---|
| `power/src/fgd-kernel-mode.c`, `power/src/build-fgd.sh` | Replacement for Android's `fuelgauged` handshake: hands the gauge to the kernel's own algorithm |
| `power/systemd/fgd-kernel-mode.service` | Its oneshot unit |
| `power/tools/usb-host-current.py`, `power/systemd/omarchy-usb-host-current.service` | 500 mA input ceiling for USB SDP (computer port) only |
| `power/tools/USB-HOST-CURRENT.md` | Full design/ownership/rollback notes of that helper (copied from the working notes) |
| `power/tools/tablet-power-status.py` | Read-only JSON power/thermal/sleep snapshot |
| `power/tools/tablet-battery-log.py` + `.service` | Read-only 30 s CSV logger for the calibration cycle |
| `power/tools/battery-cycle-report.py`, `fetch-battery-log.sh` | Desktop-side analysis of the logs |
| `power/staged/omarchy-sleep-disabled.conf` | Proposed sleep guard, **not installed** |
| `power/tests/` | Offline tests (fake sysfs, fake kernel socket) |

## Status

| Item | State | Notes |
|---|---|---|
| External power / charging from USB | Works | Charger `5-006a` (SouthChip SC8989x) online; battery ~4.33 V at termination; sink/device role after a cable replug |
| Battery percentage | Works, **estimate only** | Kernel-mode GM3.0 gauge in NVRAM-fail fallback (`info_is_nvram_fail_mode=1`) |
| Full / design capacity reported to UPower | Wrong | `charge_full` 2946000 µAh vs `charge_full_design` 294000 µAh (10× too small) |
| 500 mA ceiling on a USB SDP host port | Works | Stopped VBUS/USB cycling; survived reboot (2026-10-28) |
| Wall (DCP) / USB-PD chargers with the ceiling helper | Unverified | Helper restores the original policy for CDP/DCP/PD by design; never tested physically |
| Battery capacity calibration | **In progress** | Logger installed 2026-10-29; a full discharge ran on 2026-10-30; results pending, no capacity figure is claimed here |
| Suspend / resume (s2idle) | Untested | Only `freeze mem`/`[s2idle]` exposed; lock path issue below |
| Charger-only (power-off) boot | Untested | Needs physical access |
| Thermals | Read normally | CPU/SoC/GPU ~35-38 °C, battery 29-30 °C; charger `-127` °C is an "unavailable" sentinel |
| CPU/DDR overclocking | Not possible / no gain | See last section |

## Prerequisites

- The bring-up from the earlier docs: the vendor modules loaded at boot, in particular
  `mt6358_battery.ko` (creates `/sys/class/power_supply/battery`),
  `mtk_charger_framework.ko` and `sc8989x_charger.ko`, and an `omarchy-hw.service` that
  the units here order after. These come from your own device's vendor_boot/vendor_dlkm
  partitions; see the kernel/boot docs for extraction.
- The hardware bring-up script also sets the Type-C `preferred_role` to `sink`. Without
  it the port stayed `source`/`host` and the battery discharged (~0.5 A) while plugged
  into the desktop; requests to `power_role=sink`/`data_role=device` returned EPERM,
  and a physical unplug/replug was what finally gave `device`/`sink`.
- `tools/tablet` (root SSH). An arm64 C compiler (on the tablet, or a cross compiler) for
  `fgd-kernel-mode`. Python 3 on both sides.

## Reproduce

All copy commands below are equivalent to what the notes record (destination paths,
owners, modes); the exact copy invocations were not saved.

### 1. Gauge: `fgd-kernel-mode`

On Android, `fuelgauged` registers with the MediaTek gauge driver over a netlink socket.
Without it `capacity` stays `-1`. `fgd-kernel-mode` sends exactly two messages on
`NETLINK_FGD` (26): `SET_DAEMON_PID`, then `SEND_VERSION_CONTROL` with deliberately
non-matching protocol sizes. The driver's own reaction to that mismatch is "enable
Kernel mode Gauge": it starts the GM3.0 algorithm compiled into the driver, which then
publishes the percentage. The tool only reads sysfs and sends those two datagrams.

```sh
power/tests/run-fgd-tests.sh                 # host protocol tests against a fake kernel (12 PASS)
# build on the tablet (copy the source over first) or cross-compile:
CC=aarch64-linux-gnu-gcc power/src/build-fgd.sh       # -> power/out/fgd-kernel-mode
tools/tablet 'cat > /opt/omarchy-tablet/fgd-kernel-mode && chmod 755 /opt/omarchy-tablet/fgd-kernel-mode' \
  < power/out/fgd-kernel-mode
tools/tablet '/opt/omarchy-tablet/fgd-kernel-mode -n'          # dry run: prints the messages, sends nothing
tools/tablet 'cat > /etc/systemd/system/fgd-kernel-mode.service' < power/systemd/fgd-kernel-mode.service
tools/tablet 'systemctl daemon-reload && systemctl enable --now fgd-kernel-mode'
```

Expected (2026-10-28): kernel log shows `SET_DAEMON_PID`, `MTK Battery algo init` and
`enable Kernel mode Gauge`; `capacity` -1 → a percentage, `init_done` 0 → 1,
`charge_counter` 0 → 2946000; charger limits unchanged. Exit codes: 0 ok, 2 refused
pre-flight (already initialised/disabled/not an MTK gauge), 3 kernel did not answer,
4 capacity stayed -1.

Build note: the deployed binary was a static musl build (Android clang r416183b + Alpine
musl sysroot). `build-fgd.sh` uses any C compiler; that path was only compiled on the
desktop (x86_64, glibc), not run on the device. The installed unit was not saved; the
unit here applies the three changes the notes record (path under `/opt/omarchy-tablet`,
`After=omarchy-hw.service`, no `SuccessExitStatus=2`).

### 2. USB SDP input ceiling (500 mA)

The vendor charger framework selects `usb_charger_current=1200000` (1.2 A) even for an
SDP port, while the desktop's port advertises 500 mA. During 1080p video work the
tablet showed physical VBUS/USB cycling; it stopped with a 500 mA ceiling.

```sh
python3 power/tests/test-usb-host-current.py        # 13 offline tests
tools/tablet 'install -d -m 755 /opt/omarchy-tablet/diagnostics &&
  cat > /opt/omarchy-tablet/diagnostics/usb-host-current.py && chmod 755 /opt/omarchy-tablet/diagnostics/usb-host-current.py' \
  < power/tools/usb-host-current.py
tools/tablet 'cat > /etc/systemd/system/omarchy-usb-host-current.service && chmod 644 /etc/systemd/system/omarchy-usb-host-current.service' \
  < power/systemd/omarchy-usb-host-current.service
tools/tablet 'systemd-analyze verify /etc/systemd/system/omarchy-usb-host-current.service && systemctl daemon-reload &&
  systemctl enable --now omarchy-usb-host-current'
```

Verify:

```sh
tools/tablet 'cat /sys/devices/platform/charger/input_current \
  /sys/class/power_supply/mtk-master-charger/input_current_limit /run/omarchy-usb-current/owned.json'
```

Expect platform policy `500000`, hardware limit at most `500000` once the charger wakes,
and an ownership record. Then watch for physical VBUS stability under load; a policy
readback alone does not prove the actual current (USB-PD can override the cap).

What it does: it first checks the exact board (`/proc/device-tree/model` = `MT8781V/CA`,
compatible `mediatek,MT6789`, SouthChip supply) and the GNU build IDs of the loaded
`mtk_charger_framework` and `sc8989x_charger` modules (hard-coded for the 5.10.149
vendor build; on other firmware it refuses and you must re-derive them). Every 2 s it
reads standard supply attributes; with an online SDP and policy `-1` it records
ownership and writes `500000` to the standard `input_current_limit`. It leaves any
foreign value alone, holds through unknown/offline transients, and restores `-1` on a
known CDP/DCP/PD source or on stop. Details: `power/tools/USB-HOST-CURRENT.md`.

Why these exact writes (from disassembly of the running modules, summarised):
the standard `input_current_limit` property stores the value unchanged as the thermal/user
ceiling; `-1` means "no added ceiling"; `sc8989x_set_icl` divides by 1000, so `500000` is
500 mA (hardware clamps 100..3000 mA in 50 mA steps). The separate platform
`/sys/devices/platform/charger/input_current` store turns negative values into **0**: never
restore through it. The hardware getter's `1200000` is not the original policy: never
"restore" by writing that. Family source used to name fields: HelloVolla
`android_kernel_volla_mt8781` branch `volla-16.0`, `drivers/power/supply/{mtk_charger.c,
mtk_basic_charger.c,sc8989x_charger.c}`.

Manual equivalent (as recorded for the root trial):

```sh
printf '%s\n' 500000 > /sys/class/power_supply/mtk-master-charger/input_current_limit   # apply
printf '%s\n' -1     > /sys/class/power_supply/mtk-master-charger/input_current_limit   # restore
```

### 3. Read-only status snapshot

```sh
python3 power/tests/test-power-status.py     # 5 fixture tests
tools/tablet 'cat > /opt/omarchy-tablet/diagnostics/tablet-power-status.py && chmod 755 /opt/omarchy-tablet/diagnostics/tablet-power-status.py' \
  < power/tools/tablet-power-status.py
tools/tablet 'python3 /opt/omarchy-tablet/diagnostics/tablet-power-status.py'
```

JSON with percentage *confidence*, external supplies, real USB roles, voltage/current,
temperatures (with sentinel handling), supported sleep states, active wake sources and
RTC wake. It deliberately omits time-to-full/runtime and the synthetic charge counter.
Sysfs reads only; no netlink, no setters, no suspend.

### 4. Calibration logger and cycle report

```sh
tools/tablet 'cat > /opt/omarchy-tablet/diagnostics/tablet-battery-log.py && chmod 755 /opt/omarchy-tablet/diagnostics/tablet-battery-log.py' \
  < power/tools/tablet-battery-log.py
tools/tablet 'cat > /etc/systemd/system/tablet-battery-log.service' < power/tools/tablet-battery-log.service
tools/tablet 'systemctl daemon-reload && systemctl enable --now tablet-battery-log'
power/tools/fetch-battery-log.sh            # copies /var/lib/tablet-battery-log/*.csv to power/log/ and reports
```

Every 30 s one CSV row in `/var/lib/tablet-battery-log/<UTC date>.csv` (~0.5 MB/day):
percentage, voltage, current (negative = battery supplying the system), status,
temperature, charge counter, charger online/status/USB type/input limit, backlight. The
unit runs with `ProtectSystem=strict`; only its state directory is writable.

The report integrates current over each unplugged stretch (gaps > 5 min or a reboot end
a stretch) and prints mAh drawn vs percent dropped, i.e. implied usable capacity, plus
voltage at each 10 % step. It needs at least 30 % of unplugged discharge for an overall
figure. Validated only with synthetic data (1000 mAh over 30 % → 3333 mAh).

Method (planned 2026-10-29): wall charger to 100 % → unplugged normal use on Wi-Fi down to
~15 % (no forced deep discharge) → full recharge. Compare the implied capacity with the
gauge's 2946 mAh and the ~4000 mAh retail rating before deciding on any metadata
correction. **Status 2026-10-30:** a full discharge has just been run; the analysis is
pending and nothing about real capacity is concluded yet.

A desktop USB port is not enough for a full charge: at install time, on the 500 mA port,
the charger said `Full` with zero charge current while the battery supplied
140-190 mA under load and the percentage fell (99 → 94 %).

## Findings and pitfalls

- **The percentage is an estimate.** The gauge runs in kernel mode with
  `info_is_nvram_fail_mode=1` (Android's NVRAM calibration daemon is absent). RTC UI SOC
  can carry across normal reboots, so "99 % again after reboot" is not an independent
  measurement.
- **Design capacity is wrong by 10×.** `charge_full_design` = 294000 µAh, `charge_full`
  = 2946000 µAh, so UPower shows 12.77 Wh full vs 1.27 Wh design and nonsense health and
  time estimates. The DT gauge node has no `g_Q_MAX`, so the driver keeps its header
  default qmax 2946; that explains why full charge equals the battery0/T0 default. It is
  not a reason to patch the table, boot image or PMIC.
- **Profiles are present.** The live DT has six 100-row OCV/resistance profiles that
  match the stock overlay byte for byte and are identical to each other; parsed
  `/proc/battery/dump_log` shows `ACTIVE_TABLE 6`, `CAR_TUNE_VALUE 1010`, `R_FG_VALUE 50`
  (internal scaling of DT 101 and 5). A `0` from the standalone `car_tune_value` getter does
  not mean "no calibration". "Missing battery profiles" was a false lead.
- The battery supply can say `Charging` while the charger supply says `Full` with
  near-zero current (termination). Do not read `battery/status` alone.
- `charge_counter` is UI SOC × qmax, not a coulomb counter: don't use it to validate
  capacity.
- Never write `-1` to the platform `input_current` (becomes 0 = no input current), and
  never "restore" the 1.2 A hardware readback as if it were the policy.
- No helper here changes voltages, raw registers, modules, firmware partitions,
  CPU/thermal thresholds or watchdogs.

## Suspend (untested)

- Only `freeze mem` and `[s2idle]` are offered; no deep or hibernate. RTC and GPIO-key
  wake are enabled. That shows interfaces exist, not that resume works.
- Long-held kernel wake sources were seen: `ultra wakelock`, `USB suspend lock`,
  `disp_crtc0_wakelock`, `cmdq_0_pm_lock`. Display/audio/USB ordering needs a
  coordinated test. Never force-release wake locks to hide blockers.
- **Lock trap:** Omarchy's `omarchy-sleep-lock.service` (user manager) takes a delay
  inhibitor and locks the shell on `PrepareForSleep` regardless of idle-lock settings.
  With `hypr`'s password locked and touch unlock unvalidated, a suspend could strand the
  session behind a lock screen.
- Proposed guard, not installed: copy `power/staged/omarchy-sleep-disabled.conf` to
  `/etc/systemd/sleep.conf.d/` (disables all four sleep modes) and mask
  `omarchy-sleep-lock.service` for `hypr`. Installed Omarchy 4.0.6 does **not** detect
  this file to hide its menu entries (upstream source does). On 2026-10-28 the power key
  was ignored by a bring-up drop-in; by 2026-10-29 the notes record that the physical
  power button opens Omarchy's power menu (shutdown/restart only).
- Recorded plan for a supervised test: physical access and power-key/RTC recovery ready;
  stop playback; DPMS off through Hyprland; check which wake locks release; set and verify
  an RTC alarm; re-enable sleep only for the test; request s2idle; then check RTC/power-key
  wake, display redraw, touch, rotation, Wi-Fi reconnect, USB charging and audio.

## No overclocking (2026-10-29)

- CPU frequency/voltage tables come from MediaTek firmware (`mtk-cpufreq-hw`); Linux can
  only pick from them. Going higher means patching firmware voltage tables, with risk of
  instability, watchdog resets or damage. Not done.
- The CPUs already reach their rated maximum: `time_in_state` showed ~7.7 h at 2.2 GHz on
  the big cores since boot; little cores reach 2.0 GHz. No cap to lift.
- DDR scales automatically (1600/2400/3200 MHz seen). Forcing the top DVFSRC level
  (`/sys/kernel/helio-dvfsrc/dvfsrc_force_vcore_dvfs_opp=0`) held 3200 MHz (4266 was not
  reached) and made no measurable difference to page loads; restored to automatic (255).
  See [12-performance.md](12-performance.md).

## Rollback

```sh
tools/tablet 'systemctl disable --now tablet-battery-log'
tools/tablet 'systemctl disable --now omarchy-usb-host-current'   # ExecStopPost restores an owned -1 policy
tools/tablet 'cat /sys/devices/platform/charger/input_current'   # expect -1 if the helper owned the cap
tools/tablet 'systemctl disable --now fgd-kernel-mode'           # takes effect next boot; the gauge stays in kernel mode until then
```

`usb-host-current.py --restore` performs one owned-only cleanup; it never resets a value
it did not set.
