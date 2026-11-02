# 04 - Installing Omarchy on the Arch Linux ARM root filesystem

This step turns the booted Arch Linux ARM (ALARM) root filesystem into a packaged
Omarchy 4.0.6 desktop. It uses the official signed aarch64 packages from
`pkgs.omarchy.org`, Omarchy's default app manifest, a UWSM session started by a
system unit, and a small set of user overrides. No Omarchy ISO, Limine, replacement
kernel or repartitioning is involved. The Android boot chain from docs 01-03 stays
as it is.

Installed state recorded on 2026-10-28: `omarchy 4.0.6-1`, `omarchy-settings 4.0.6-1`,
the full 147-entry default manifest, Quickshell `0.3.2.r7.g2b4501a-1` from the Omarchy
repository, and Hyprland `0.56.2-4` from Arch Linux ARM (Hyprland commit
`efb50993780079460b0cbed1363e2166a2de1d9f`).

## Status

| Item | State |
|---|---|
| Official `omarchy` + `omarchy-settings` 4.0.6-1, signature checking on | Works |
| Full default app manifest (147 entries resolvable) | Installed |
| UWSM session with a real logind/PAM session and user D-Bus (`/run/user/1001`) | Works, survives normal reboot |
| Omarchy shell (Quickshell), Tokyo Night theme, menu, Apps search | Works. Apps touch was physically confirmed after the bar touch fix |
| Chromium (sandbox on), Files | Render and launch |
| `pacman -Qkk omarchy` (packaged files untouched) | 1769 files, 0 altered |
| No failed system/user units, empty `hyprctl configerrors` after reboot | Observed on 2026-10-28 |
| Typing and opening Apps/Term usable (software rendering profile) | Physically confirmed |
| Account password | Set 2026-10-30 (`sudo` works) |
| Lock screen with touch unlock | Works since 2026-11-02: short PIN, power key, idle lock after 5 min ([docs/10](10-touch-ui.md#lock-screen)) |
| Native menu search + launch by physical touch | Pending at the time of the notes (injected input passed; not counted as physical proof) |
| Suspend | Untested ([docs/11](11-power-battery.md)) |

Later work changed parts of this baseline. The GPU compositor path is in [docs/08](08-gpu.md).
The Option A touch shell and the keyboard Desktop drawer are in [docs/10](10-touch-ui.md); they
replace the bar buttons created here. Gaps/borders 0, monocle layout and window animations
are in [docs/12](12-performance.md). Those docs own their changes; this one describes the base install.

## Prerequisites

- A booting ALARM root filesystem on the tablet with the vendor kernel, systemd, the
  vendor hardware unit, SSH over the USB link, and DRM/touch working. That is docs 01-03.
  The ALARM `linux-aarch64` kernel, DTBs and modules were deliberately left out of the root
  filesystem image, since that kernel cannot run on this device.
- Working network on the tablet to reach the package mirrors. In this project that was
  Wi-Fi with `wpa_supplicant@wlan0` + systemd-networkd at first
  ([docs/05](05-wifi-bluetooth.md)). Earlier, the USB link to the desktop could also carry traffic.
- A `hypr` user with uid **1001**. The unit and env files hard-code `/run/user/1001`. ALARM
  ships `alarm` as uid 1000, so the next `useradd` gets 1001. The prototype created it with
  (from the graphics test script): `useradd -m -G video,input,render,seat hypr`.
- `seatd.service` (the unit requires it). The tablet also has a drop-in with
  `Environment=SEATD_VTBOUND=0`, which belongs to the graphics/session bring-up ([docs/08](08-gpu.md)).
- The graphics pieces the session env refers to: `/opt/omarchy-tablet/drm-no-cursor.so`,
  `/opt/omarchy-tablet/aq-software-primary.so` and `/opt/omarchy-tablet/gpu/start-hyprland`
  ([docs/08](08-gpu.md)). The on-screen keyboard helper `/opt/omarchy-tablet/ui/tablet-ui`
  ([docs/10](10-touch-ui.md)) and `/opt/omarchy-tablet/tablet-orientation` ([docs/06](06-sensors.md)).
- On the desktop: this repo and the SSH helper `tools/tablet` (key in
  `${TABLET_SSH_KEY:-$HOME/.ssh/omarchy-tablet_ed25519}`). Every `tools/tablet ...` command
  below runs as root on the tablet.

## Files in this repo

| Repo path | Installed to | Purpose |
|---|---|---|
| `omarchy/resolve-defaults.py` | run on the tablet as root | Resolves Omarchy's default manifest against the configured repos without installing anything |
| `omarchy/staged/*` | `/opt/omarchy-tablet/omarchy-install/staged/` | Staging directory read by `setup-user.py` and the desktop unit |
| `omarchy/staged/setup-user.py` | run once as root | Applies the user overrides to `/home/hypr` |
| `omarchy/staged/prepare-runtime.py` | `ExecStartPre` of the desktop unit | Builds the root-owned Omarchy runtime overlay with the tablet adaptations |
| `omarchy/staged/{monitors,looknfeel,autostart}.lua` | `~hypr/.config/hypr/` (copied by `setup-user.py`) | Hyprland user overrides |
| `omarchy/staged/shell.toml` | `~hypr/.config/omarchy/themes/tokyo-night/shell.toml` | Larger fonts/bar for the panel |
| `omarchy/staged/TabletButton.qml` | `~hypr/.config/omarchy/bar/modules/` | Touch bar button widget |
| `omarchy/uwsm/20-tablet` | `~hypr/.config/uwsm/env.d/20-tablet` | Session environment (hardware adaptations) |
| `omarchy/systemd/omarchy-desktop.service` | `/etc/systemd/system/` | Starts the UWSM session on tty1 without a display manager |
| `connectivity/networkmanager/setup-network.py` | run once as root | Moves Wi-Fi to NetworkManager ([docs/05](05-wifi-bluetooth.md)) |
| `omarchy/screenshots/software-profile-final.png` | - | The desktop after the final software-profile reboot (portrait, 2026-10-28 18:11) |

`omarchy-aarch64.db` in the private tree was a copy of the `[omarchy]` aarch64 sync
database fetched on 2026-10-28 (zstd tar, 179 packages; it lists
`omarchy-4.0.6-1-aarch64.pkg.tar.zst` with SHA-256
`27cd6d88fda0be6c269c14dd65dad6a014384fedc3ea4eb5c742b2b972b10494`). It was only used to
inspect the package set offline and is not shipped. pacman downloads the current one.

## Reproduction steps

The notes record the results of steps 1-6 and the files involved, but not every command typed.
Where a command below is reconstructed rather than recorded, it says so.

### 1. Back up first

The project made a root-only backup before installing:
`/opt/omarchy-tablet/backups/pre-omarchy-<timestamp>/` (directory 0700) with
`config.tar.gz` (0600; `/etc` configuration and the home directory, so it contains
secrets such as Wi-Fi credentials), the package list and the original `pacman.conf`. The
exact archive command was not recorded. Equivalent (reconstructed):

```sh
tools/tablet 'set -e; B=/opt/omarchy-tablet/backups/pre-omarchy-$(date +%Y%m%d-%H%M%S);
  install -d -m 0700 $B; pacman -Qq > $B/packages.txt; cp -a /etc/pacman.conf $B/;
  tar -czf $B/config.tar.gz /etc /home; chmod 0600 $B/config.tar.gz'
```

### 2. Configure pacman

Three changes on top of the ALARM `pacman.conf`. The resulting file is not in the notes;
this is reconstructed from the notes and from upstream Omarchy's own aarch64 config
(`default/pacman/aarch64/pacman-stable.conf` at commit
`526fe64706b0fcbaf0483ac7253cd62abe7854ae` of https://github.com/omacom/omarchy):

```ini
[options]
# ... keep the existing ALARM options ...
# The vendor 5.10 kernel has no Landlock (added in 5.13), so pacman's download sandbox cannot work.
DisableSandbox
# The ALARM mainline kernel package is useless here and must never be installed or upgraded.
IgnorePkg = linux-aarch64
SigLevel = Required DatabaseOptional

# Omarchy first, as on x86_64: a package it also publishes comes from Omarchy.
[omarchy]
Server = https://pkgs.omarchy.org/stable/$arch

[core]
Include = /etc/pacman.d/mirrorlist
[extra]
Include = /etc/pacman.d/mirrorlist
[alarm]
Include = /etc/pacman.d/mirrorlist
```

The project kept the existing ALARM mirrors. Upstream Omarchy's aarch64 config instead
points the mirrorlist at its own ALARM mirror (`https://arm-stable-mirror.omarchy.org/$arch/$repo`).

### 3. Trust the Omarchy signing key

Fingerprint `40DFB630FF42BCFFB047046CF0134EE680CAC571`. The project imported the key from a
desktop that already trusted it (command not recorded). Omarchy's own procedure
(`bin/omarchy-update-keyring` upstream) is:

```sh
tools/tablet 'pacman-key --recv-keys 40DFB630FF42BCFFB047046CF0134EE680CAC571 --keyserver keys.openpgp.org &&
  pacman-key --lsign-key 40DFB630FF42BCFFB047046CF0134EE680CAC571'
```

Keep `SigLevel = Required`. Signature checking stayed on throughout.

### 4. Install sudo, Omarchy and the default manifest

`sudo` is an ISO prerequisite that the minimal ALARM root lacks. Then install the two
Omarchy packages and their dependencies (reconstructed; the notes record the result, not
the command line):

```sh
tools/tablet 'pacman -Syu --needed sudo omarchy omarchy-settings'
```

Resolve Omarchy's default manifest (`/usr/share/omarchy/install/omarchy-base.packages`,
147 entries after stripping comments). The script removes entries that are not available
from the configured repos and writes the result under `/opt/omarchy-tablet/omarchy-install/`:

```sh
tools/tablet 'install -d /opt/omarchy-tablet/omarchy-install && cat > /opt/omarchy-tablet/omarchy-install/resolve-defaults.py' < omarchy/resolve-defaults.py
tools/tablet 'python3 /opt/omarchy-tablet/omarchy-install/resolve-defaults.py'
# prints "<n> manifest entries resolvable, <m> unavailable" and the unavailable names
```

All entries resolved on the tablet. Install the resolvable list (reconstructed command):

```sh
tools/tablet 'pacman -S --needed - < /opt/omarchy-tablet/omarchy-install/defaults-available.txt'
```

The project kept install logs under `/var/log/tablet-omarchy-*` and inventories under
`/opt/omarchy-tablet/omarchy-install/`. Note that `resolve-defaults.py` reads only the base
list. Upstream's aarch64 addition list (`omarchy-aarch64.packages`, currently just
`zram-generator`) is not included.

### 5. Make sure Quickshell comes from the Omarchy repo

The full-defaults install selected Arch Linux ARM's older Quickshell. The project then
restored the official build `0.3.2.r7.g2b4501a-1` (exact command not recorded; for example
`pacman -S omarchy/quickshell`). Check with `pacman -Si quickshell` / `pacman -Q quickshell`.

### 6. Audio packages and group

The default manifest did not pull in a complete PipeWire stack. These were added later
with Omarchy's own helper, as recorded:

```sh
omarchy pkg add pipewire-audio pipewire-alsa pipewire-pulse rtkit
```

This vendor kernel has no POSIX ACL support on device nodes (`CONFIG_TMPFS_POSIX_ACL` is
off), so logind's usual `uaccess` grant cannot give the user ALSA access. `hypr` must be in
the `audio` group (`setup-user.py` runs `usermod -aG audio hypr`). The desktop unit also lists
`audio` in `SupplementaryGroups`. Audio routing itself is in [docs/07](07-audio.md).

### 7. Provision the `hypr` user

The project seeded the account from the packaged `/etc/skel`, then ran Omarchy's
first-install user provisioning as `hypr`:

```sh
# as hypr, in a session where /run/user/1001 exists
OMARCHY_SETUP_CONTEXT=provision-owner omarchy-provision-user --first-install
```

With `OMARCHY_SETUP_CONTEXT=provision-owner`, the Node bootstrap uses the native ARM64
download. Omarchy's first-run setup also completed. The notes do not record how the skel
was copied into the existing home directory or how first-run was driven.

### 8. Stage and apply the tablet overrides

```sh
tools/tablet 'install -d -m 0755 /opt/omarchy-tablet/omarchy-install/staged'
tar -C omarchy/staged -cf - . | tools/tablet 'tar -C /opt/omarchy-tablet/omarchy-install/staged --no-same-owner -xf -'
tools/tablet 'python3 /opt/omarchy-tablet/omarchy-install/staged/setup-user.py'
tools/tablet 'install -d -o hypr -g hypr /home/hypr/.config/uwsm/env.d && cat > /home/hypr/.config/uwsm/env.d/20-tablet && chown hypr:hypr /home/hypr/.config/uwsm/env.d/20-tablet' < omarchy/uwsm/20-tablet
```

`setup-user.py` expects `~hypr/.config/omarchy/shell.json` and `~hypr/.config/foot/foot.ini`
from the skel to exist already. It runs as root and does not change ownership of the files
it writes. The notes do not record a `chown` afterwards. Check that everything under
`/home/hypr/.config` is owned by `hypr`, because the shell writes `shell.json` itself:

```sh
tools/tablet 'find /home/hypr/.config ! -user hypr -ls'
```

### 9. Install the desktop unit

```sh
tools/tablet 'cat > /etc/systemd/system/omarchy-desktop.service' < omarchy/systemd/omarchy-desktop.service
tools/tablet 'systemctl daemon-reload && systemctl enable omarchy-desktop.service'
```

The unit's `ExecStart` uses the GPU wrapper `/opt/omarchy-tablet/gpu/start-hyprland`
([docs/08](08-gpu.md)). Without its enable markers, that wrapper runs the packaged
`/usr/bin/start-hyprland` in the software profile. If the wrapper is not installed at all,
change the line to the software-only form the tablet used before the GPU work:

```
ExecStart=/usr/bin/uwsm start -F -e -D Hyprland -- /usr/bin/start-hyprland -- --config /home/hypr/.config/hypr/hyprland.lua
```

There is no display manager. The unit is `WantedBy=multi-user.target` and does not start in
charger mode (`/run/omarchy-hw/charger-mode`, created by the hardware unit in [docs/03](03-boot-and-rootfs.md)).
The prototype's fallback config `/opt/omarchy-tablet/hyprland.lua` can stay where it is.

### 10. Network and browser

- Move Wi-Fi to NetworkManager so Omarchy's network panel can manage it:
  [docs/05](05-wifi-bluetooth.md), "NetworkManager".
- Add `--no-first-run` to the user's Chromium flags. The unbranded build otherwise opens a
  blank introductory terms window.

### 11. Reboot and verify

```sh
tools/tablet 'systemctl reboot'
tools/tablet 'systemctl --failed; systemctl -M hypr@ --user --failed'
tools/tablet 'pacman -Qkk omarchy | tail -n 2'          # expect 0 altered files
tools/tablet 'cat /etc/omarchy.conf'                    # OMARCHY_PATH=/opt/omarchy-tablet/omarchy-runtime
tools/tablet 'journalctl -b -u omarchy-desktop.service | grep "Prepared Omarchy runtime"'
tools/tablet 'su hypr -c "XDG_RUNTIME_DIR=/run/user/1001 HYPRLAND_INSTANCE_SIGNATURE=\$(ls /run/user/1001/hypr | head -n1) hyprctl configerrors"'
```

Expected (as observed on 2026-10-28): Omarchy bar at 800x48 logical pixels, keyboard at
800x360, `uwsm finalize` done, polkit agent registered, no failed units, empty configerrors.
The `systemctl -M` and `hyprctl` invocations above are convenience forms. The notes record the
results, not these exact command lines.

## Account and sudo state

- `hypr` is in `wheel`. `sudo` is installed with a wheel policy that **requires a password**.
  No passwordless administrator rule was added.
- **No password is set for `hypr`.** A password request to the user expired without an
  entry, and none was invented. In practice `sudo` is unusable for `hypr` until root sets a
  password (root access is the SSH key over the USB link).
- **Idle lock**: `setup-user.py` writes `"idle": {"screensaver": 0, "lock": 0}` into
  `shell.json` so that Omarchy's password lock screen, which cannot be used by touch, never
  starts. Since 2026-11-02 the tablet has its own PIN lock screen and
  `shell-plugin/deploy-lock.sh` sets `idle.lock` to 300 s ([docs/10](10-touch-ui.md#lock-screen)).
  Until a PIN is set, nothing locks.
- **Password**: on 2026-10-30 a password was set for `hypr` (entered by the owner, applied
  with `chpasswd` over the USB SSH link), so `sudo` works. The lock screen does not use it.

## The user overrides and why each exists

| Override | What | Why |
|---|---|---|
| `monitors.lua` | DSI-1 at the native `1200x1920@60`, scale 1.5, transform read from `~/.local/state/omarchy/tablet-transform`. The touch device gets the same transform. `GDK_SCALE=1` | Keeps the panel's native mode. Restores the last rotation at startup with touch aligned ([docs/06](06-sensors.md)) |
| `looknfeel.lua` (the shipped copy is the final 2026-10-29 state: window animations off but layer fades on, `monocle` layout, gaps/borders 0; see [docs/12](12-performance.md)) | Base install: animations, blur and shadow off. `direct_scanout=0`, `use_fp16=0`, `cm_enabled=false`, `new_render_scheduling`, software cursor, solid Tokyo Night background colour, `damage_tracking=2`, all windows opaque, no key repeat for the wvkbd virtual keyboard, wvkbd layer order -100 | The CPU renders everything (llvmpipe). Blending and full redraws are expensive. A delayed touch release used to turn one tap into repeats. The keyboard must stay above menu scrims to receive touch |
| `autostart.lua` | `uwsm finalize`, keyboard (`tablet-ui keyboard-start`), orientation watcher, Foot as a UWSM service | Omarchy's packaged autostart still starts its shell and services. These are the tablet additions |
| `uwsm/env.d/20-tablet` | See the table below | Hardware adaptations for an otherwise packaged session |
| `shell.json` (edited by `setup-user.py`) | Idle lock off. Bar layout Apps, Rotate, Auto / clock / Keys, Term, Close, audio, network, power, as `TabletButton.qml` widgets | Touch-sized controls. Later replaced by the Option A shell and keyboard drawer ([docs/10](10-touch-ui.md)) |
| `themes/tokyo-night/shell.toml` | `font.base-size = 16`, `bar.size-horizontal = 36` | Readable on an 8.4" panel at scale 1.5 |
| `TabletButton.qml` | Derived from Omarchy's `WidgetButton` | The stock `command` bar module emits a read-only `moduleName` property warning. This widget uses Omarchy's documented QML injection and its click registration |
| `foot.ini` font 9 -> 13 | In place, by `setup-user.py` | Legibility |

Session environment (`omarchy/uwsm/20-tablet`):

| Variable | Reason |
|---|---|
| `LIBSEAT_BACKEND=seatd` | Seat access through seatd |
| `UWSM_APP_UNIT_TYPE=service` | Apps launched as services, as the GPU integration expects |
| `LD_PRELOAD=.../drm-no-cursor.so:.../aq-software-primary.so`, `AQ_TABLET_SOFTWARE_PRIMARY=1`, `TABLET_FINISH_SOFTWARE_FRAMES=1` | Cursor ioctl shim. Skips Aquamarine's useless secondary-renderer attempts on the software DRM node. Maps Hyprland's `glFlush` to `glFinish` (the cause of partial-window flicker) ([docs/08](08-gpu.md)) |
| `AQ_NO_ATOMIC=0`, `AQ_NO_MODIFIERS=1`, `AQ_DRM_DEVICES=/dev/dri/card0` | Atomic KMS (selected profile). No modifiers. The only DRM node (there is no render node) |
| `GBM_ALWAYS_SOFTWARE=1`, `LIBGL_ALWAYS_SOFTWARE=1`, `GALLIUM_DRIVER=llvmpipe`, `LP_NUM_THREADS=8` | Software rendering baseline for applications on all eight cores (pinning to the big cores was slower for large redraws) |
| `TABLET_PLAIN_BACKGROUND=1` | Hides the wallpaper surface. Hyprland clears to a solid colour instead (see `prepare-runtime.py`) |
| `QT_QPA_PLATFORM=wayland`, `QSG_RHI_BACKEND=opengl`, `QSG_RENDER_LOOP=basic`, `GSK_RENDERER=cairo` | Toolkit settings kept from the validated prototype. The notes do not record the effect of each one separately |

## How the runtime overlay works (`prepare-runtime.py`)

Omarchy's menu is a full-screen layer with an input-catching scrim, which covers an
on-screen keyboard. A user-plugin clone of the menu was tried first, but its scoped context
produced an empty app list (it was disabled and moved to `~/.config/omarchy/disabled-plugins`).

The final approach never edits `/usr/share/omarchy`. On every desktop start
(`ExecStartPre=+`, as root) the script:

1. builds `/opt/omarchy-tablet/omarchy-runtime.next`, where every top-level entry of
   `/usr/share/omarchy` except `shell` is a symlink to the installed package, and `shell` is
   copied;
2. patches the copy:
   - `plugins/menu/Menu.qml`: the menu stops 360 logical px (portrait) or 260 px
     (landscape) above the bottom, so the keyboard sits outside the scrim. Opening a menu
     runs `tablet-ui keyboard-show`. Keyboard focus is `OnDemand`. The key-catcher takes
     focus when the menu maps or its search header is tapped (touch `TapHandler`);
   - `plugins/bar/Bar.qml`: a touchscreen-only `TapHandler` (`ReleaseWithinBounds`) calls the
     bar's own click dispatcher. Qt's synthesized mouse click was being cancelled by the
     bar's reorder `MouseArea` after tiny finger movement. **This was the fix for "Apps does
     not respond"** (user confirmed "Apps opens now");
   - `plugins/background/Background.qml`: opaque instead of transparent, and hidden when
     `TABLET_PLAIN_BACKGROUND=1`;
   - `Ui/KeyboardPanel.qml`, `Ui/PopupCard.qml`, `Ui/WidgetButton.qml`, `plugins/bar/Bar.qml`:
     finite transition durations set to 0. Files containing `loops:` are refused;
3. rotates `omarchy-runtime` -> `omarchy-runtime.previous`, `.next` -> `omarchy-runtime`, and
   writes `/etc/omarchy.conf` with `OMARCHY_PATH=/opt/omarchy-tablet/omarchy-runtime`.

Each patch checks that the exact upstream text is present and otherwise **stops with a
review message**. Because it is an `ExecStartPre`, a changed upstream menu/bar/background
after an Omarchy update stops the desktop from starting until the patch is reviewed. That
is intentional: an incorrect patch is worse than none.

## Pitfalls and lessons

- **pacman needs `DisableSandbox`.** The 5.10 kernel has no Landlock LSM (the stock
  `CONFIG_LSM` list has none, and Landlock only appeared in 5.13).
- **`IgnorePkg = linux-aarch64`.** The ALARM kernel cannot boot here. The real kernel lives
  in the boot partitions (docs 02-03), and a pacman kernel upgrade must never touch them.
- **Repository order matters, and it can still pick the wrong package.** Quickshell ended up
  from ALARM after the full-defaults install and had to be put back. Check versions of
  Omarchy-published packages after big installs.
- **UWSM needs a real PAM/logind session.** The unit sets `PAMName=login`,
  `TTYPath=/dev/tty1` and explicit `XDG_SEAT`/`XDG_VTNR`/session variables. Without the PAM
  session, UWSM cannot find the foreground-VT session and exits before starting the
  compositor. The earlier `dbus-run-session` prototype was replaced for this reason.
- **No device-node ACLs** on this kernel. Use groups (`audio`, `video`, `render`, `input`,
  `seat`) instead of relying on `uaccess`.
- **Never run the stock `omarchy-restart-shell`** after the GPU work: it relaunches Quickshell
  inside the compositor's GPU environment and the shell surfaces come up blank. Use the
  wrapper from [docs/10](10-touch-ui.md). Related: Omarchy's packaged default autostart imports
  all compositor variables into the user manager, which leaked GPU variables into apps. The
  fix (a `~/.config/default/hypr/autostart.lua` that wraps startup in `gpu/app-env`) is in
  [docs/08](08-gpu.md).
- **Partial-window flicker was a Hyprland classification bug, not KMS.** Hyprland 0.56.2
  checks the DRM driver name for "llvmpipe". `mediatek` does not match, so it used
  `glFlush` instead of `glFinish`. Atomic mode and full damage alone did not help. The
  frame-completion shim did ("Flicker is gone, but it's still slow") ([docs/08](08-gpu.md)).
- **Synthetic input is not physical proof.** Temporary evdev/uinput injections overlapped
  real use (Chromium opened while a diagnostic typed `chr`). The notes keep injected results
  separate from physical confirmation, and so should you.
- **High load average is misleading.** About ten vendor kernel threads sleep in D state.
  That does not mean ten CPUs of runnable desktop work.
- `prepare-runtime.py` must be reviewed against each Omarchy update (see above).
- `setup-user.py` is written for the first run after skel seeding. It overwrites the bar
  layout and the Hyprland override files.

## Rollback

- Desktop: `systemctl disable --now omarchy-desktop.service`. To return to the packaged
  shell paths, remove `/etc/omarchy.conf` (or point it at `/usr/share/omarchy`). The previous
  overlay is kept as `/opt/omarchy-tablet/omarchy-runtime.previous`.
- Packages and config: the pre-install backup from step 1 holds the original
  `pacman.conf`, package list and `/etc` + home archive.
- Later stages keep their own root-only backups under `/opt/omarchy-tablet/backups/`
  (`pre-performance-rotation/`, `pre-gpu/`, touch-UI and `looknfeel.lua.*` backups). See the
  respective docs.
- Kernel and partition backups from the boot-chain work are unaffected by this step.
