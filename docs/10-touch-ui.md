# 10 - Touch UI: keyboard, bar, "Home & Dock" shell plugin, Kid Mode

This covers the touch layer on top of the native Omarchy session: a patched
[wvkbd](https://github.com/jjsullivan5196/wvkbd) on-screen keyboard that follows
text focus, a small bar change, and the `tablet.touch` Omarchy shell plugin
(design "Option A: Home & Dock") with a PIN-protected Kid Mode. Work dates:
2026-10-28 (keyboard, bar) and 2026-10-29 (shell plugin, Kid Mode, rotation and
performance fixes). Responsiveness numbers are in
[12-performance.md](12-performance.md).

Files in this repo:

| Path | What |
|---|---|
| `touch-ui/wvkbd/tablet-wvkbd-v0.20.patch` | Our changes against upstream wvkbd v0.20 (`main.c`, `keyboard.c/.h`, `layout.mobintl.h`, new `tablet-layouts.h`, `config.mobintl.h`, `Makefile`) |
| `touch-ui/wvkbd/build.sh` | Clone v0.20, verify the commit, apply the patch, build (`host` or on the `tablet`) |
| `touch-ui/tests/{keyboard-test.c,run.sh}` | Host test of the real keyboard state machine and renderer |
| `touch-ui/rootfs/tablet-ui` | Keyboard lifecycle + fixed action dispatcher (installed at `/opt/omarchy-tablet/ui/tablet-ui`) |
| `touch-ui/rootfs/install-touch-ui.py`, `TabletButton.qml` | Bar installer (keyboard toggle glyph) + its QML button |
| `touch-ui/rootfs/systemd/omarchy-fcitx5-tablet.conf` | fcitx5 user-unit drop-in (no `waylandim`) |
| `touch-ui/deploy.sh` | Install keyboard binary + helper + bar, with backups |
| `shell-plugin/plugin/tablet.touch/` | The Quickshell plugin: `manifest.json`, `TouchShell.qml`, `KidMode.qml` |
| `shell-plugin/{deploy.sh,restart-shell,qs,shot}` | Install the plugin, restart only the shell, shell IPC/log, screenshot |
| `shell-plugin/tools/touch-tap.py` | Injects real multitouch frames into `mtk-tpd` (testing) |
| `shell-plugin/design/` | The HTML design mock and its notes (why Option A) |
| `shell-plugin/screenshots/` | Three downscaled device screenshots |

## Status

| Item | State | Evidence |
|---|---|---|
| Keyboard: one character per tap, sticky Super/Ctrl/Alt/Shift, 123/Fn/Desktop pages | Works | User physically typed in Apps search and Terminal; Super → f fullscreen/restore; Desktop Copy/Paste without stuck modifiers (2026-10-28) |
| Keyboard follows text focus (`--auto`), 180 ms debounced auto-hide | Works (injected touch) | Keys rise on foot/Chromium omnibox focus and hide on tapping a page; six focus switches between two terminals caused zero keyboard close/open |
| Hide key on every page; bar keyboard glyph | Works (injected touch) | Two taps on `e` typed exactly `ee`, two Backspaces erased them |
| Home, dock, handle (tap = peek, swipe up = Home), Open apps (switch, ×) | Works in landscape (injected touch) | 2026-10-29; × closed only the chosen window |
| Rotation: handle/dock drawn at the right size after rotating | Fixed | 6/6 rotations correct by 1.5 s; screenshots in both orientations |
| Kid Mode: PIN setup, allow-list tiles, closing non-allowed windows, kid keyboard pages, leave | Works | Verified on the device by touch (2026-10-29); persisted across a shell restart |
| Re-showing keys by tapping an already focused terminal after a manual Hide | Not working | No new focus event; use the bar/handle keyboard glyph |
| fcitx5 CapsLock compose in Wayland apps | Lost (by design) | Cost of disabling fcitx5 `waylandim` |
| Notification popups in Kid Mode | Not blocked | They still show on top |
| Portrait use of the shell plugin and Kid Mode | Unverified | Only drawing after rotation was checked; layout is width-driven |
| Physical finger use/comfort of the plugin, persistence across reboot | Unverified | All pieces are persisted, but no reboot was tested after the plugin; Kid Mode reboot not tested |
| Redesigned keyboard in both orientations by physical touch | Unverified | Earlier rotation confirmations apply to the previous keyboard |
| Gesture text editing (long-press/double-tap menus) | Per app only | Comes from each app's own touch handling (Chromium/GTK); the system-wide version exists only in the HTML mock |
| `touch-ui/deploy.sh`, `wvkbd/build.sh tablet`, `shell-plugin/*` in this repo | Unverified in this form | Adapted to `tools/tablet`; the originals were used on the device |

## Prerequisites

- The native Omarchy session from the earlier docs, with:
  - user `hypr` (uid 1001), Wayland socket `wayland-1`;
  - the Omarchy shell runtime at `/opt/omarchy-tablet/omarchy-runtime/shell`, whose
    menu reserves 360/260 logical px (portrait/landscape) for the keyboard and runs
    `tablet-ui keyboard-show` when opened (see [`omarchy/`](../omarchy/));
  - the Hyprland user config that autostarts `/opt/omarchy-tablet/ui/tablet-ui keyboard-start`,
    disables key repeat for the device `hl-virtual-keyboard-wvkbd-mobintl` and puts the
    `wvkbd` layer above menu scrims (Omarchy doc / [`omarchy/`](../omarchy/));
  - `/opt/omarchy-tablet/gpu/app-env`, the wrapper that gives clients the llvmpipe
    environment (graphics doc / [`graphics/`](../graphics/));
  - `/opt/omarchy-tablet/tablet-orientation` for the Rotate/Auto actions ([`sensors/`](../sensors/)).
- `tools/tablet` working (root SSH; `TABLET_ADDRESS`, `TABLET_SSH_KEY`).
- On the tablet, to build the keyboard: `gcc make pkgconf wayland wayland-protocols libxkbcommon cairo pango`.
- On the desktop, for the host test: the same packages plus `git`, `patch`, network access to GitHub.

Nothing in this doc needs files from your own device's vendor partitions.

## Reproduce

### 1. Host test of the patched keyboard (desktop)

```sh
touch-ui/tests/run.sh
```

Expected: `PASS: 12 native renders, both geometries, hit targets, one tap/letter,
modifier chords, forced modifier release, cancellation, fixed actions, Hide on every
page, kid pages without shortcut modifiers`, and 12 PNGs under
`touch-ui/build/test/ui/evidence/`. This was re-run while preparing this repo
(2026-10-30) from a fresh v0.20 clone plus the patch; the patched tree is byte-identical
to the one that was deployed. It proves rendering and emitted input state, not
compositor focus or physical touch.

### 2. Build the arm64 keyboard on the tablet

```sh
touch-ui/wvkbd/build.sh tablet
```

This clones v0.20 (commit `6b41504a0cb58fd1163fa44692398fbd61f8905f`), applies the
patch, streams the source to `/home/hypr/src/wvkbd-tablet`, runs
`make LAYOUT=mobintl clean` and `make LAYOUT=mobintl -j4 wvkbd-mobintl` as `hypr`, and
copies the binary to `touch-ui/out/wvkbd-mobintl`. The notes recorded the same steps by
hand (copy tree to `/home/hypr/src/...`, `make` as hypr, copy back). Installed SHA-256 of
the deployed build (2026-10-29, debounced auto-hide):
`880dd17cd07766908947c4f2e31ac4923a627b5a01c9bf700ddca65ba44eac0c`. Your hash will
differ (different toolchain/paths); it is listed only to identify the deployed version.

### 3. Install keyboard, helper and bar

```sh
touch-ui/deploy.sh
```

It refuses a binary that is not arm64 or lacks the `tabletomarchy` page, backs up
`/usr/local/bin/wvkbd-mobintl` and `/opt/omarchy-tablet/ui/tablet-ui` to
`/opt/omarchy-tablet/backups/touch-keyboard-*`, installs both, runs
`install-touch-ui.py` (backs up `~hypr/.config/omarchy/shell.json` and the bar module to
`/opt/omarchy-tablet/backups/touch-ui-*`, removes old `tablet.*` bar buttons, adds one
keyboard toggle glyph at the right) and starts the keyboard hidden. No compositor
restart; the shell hot-reloads bar JSON. Differences from the original script: the
`scp` became a tar stream through `tools/tablet`, and missing files are tolerated in the
backup step (first install). The original revised script was itself only
syntax/artifact-checked; the live install was done manually before it.

### 4. Hand the input-method slot to wvkbd (fcitx5 drop-in)

`--auto` only works if wvkbd owns the Wayland input-method slot, so fcitx5 must run
without its `waylandim` frontend. The notes record the destination path and the
reload/restart, not an install script; equivalent commands:

```sh
tools/tablet "runuser -u hypr -- sh -c 'mkdir -p ~/.config/systemd/user/omarchy-fcitx5.service.d &&
  cat > ~/.config/systemd/user/omarchy-fcitx5.service.d/tablet.conf'" \
  < touch-ui/rootfs/systemd/omarchy-fcitx5-tablet.conf
tools/tablet 'runuser -u hypr -- env XDG_RUNTIME_DIR=/run/user/1001 systemctl --user daemon-reload &&
  runuser -u hypr -- env XDG_RUNTIME_DIR=/run/user/1001 systemctl --user restart omarchy-fcitx5'
```

Verify: `tools/tablet 'tr "\0" " " < /proc/$(pgrep -u hypr -x fcitx5)/cmdline'` shows
`--disable notificationitem,waylandim`. Back up `~hypr/.config/fcitx5` first if you
care about it (the original run kept a copy under `/opt/omarchy-tablet/backups/fcitx5-config-*`).

### 5. Install the shell plugin

```sh
shell-plugin/deploy.sh        # copy plugin, add {"id":"tablet.touch"} to shell.json plugins[] (backup first)
shell-plugin/restart-shell    # required after any plugin code change
```

`deploy.sh` stages beside, not inside, the watched plugins directory and swaps it in,
then asks the shell to `rescanPlugins` and `reloadConfig`. Backups:
`/opt/omarchy-tablet/backups/touch-shell-*`. `--drop-bar-buttons` additionally
strips any `tablet.*` bar entries.

`restart-shell` kills Quickshell for the Omarchy runtime and relaunches
`omarchy-launch-shell` through `gpu/app-env`, then checks that the new process has
`GALLIUM_DRIVER=llvmpipe`. Expected output: `shell restarted (llvmpipe)`.

### 6. Verify

```sh
shell-plugin/qs log                                     # shell log; expect no tablet.touch errors
shell-plugin/qs ipc call shell listPlugins
shell-plugin/qs ipc call shell call tablet.touch debug "''"    # state JSON incl. geometry
shell-plugin/qs ipc call shell summon tablet.touch "'{\"view\":\"home\"}'"   # or switcher / peek
shell-plugin/qs ipc call shell hide tablet.touch
shell-plugin/shot home                                  # grim → shell-plugin/shots/home.png
```

With an app open, tap the 34 px handle at the bottom (dock peeks), swipe up on it
(Home), tap a text field (keys rise), tap **Hide**. Optional scripted touch: install
the injector root-only and send panel-pixel taps (the compositor applies the normal
rotation/scale, like a finger):

```sh
tools/tablet 'install -d -m 700 /opt/omarchy-tablet/diagnostics &&
  cat > /opt/omarchy-tablet/diagnostics/touch-tap.py && chmod 700 /opt/omarchy-tablet/diagnostics/touch-tap.py' \
  < shell-plugin/tools/touch-tap.py
tools/tablet 'python3 /opt/omarchy-tablet/diagnostics/touch-tap.py tap 1118 737 40'   # dock "Open apps" in landscape
```

Landscape (transform 1, scale 1.5): logical (x, y) → panel `px = 1.5·y`, `py = 1920 − 1.5·x`.

## Using it

- **Keyboard** (`tablet` pages, five rows): QWERTY with Esc/Tab/Ctrl/Alt/Super/arrows/⌫
  on top; **123** (numbers, shell punctuation), **Fn** (F1-F12, Home/End/PgUp/PgDn/Ins/Del,
  and **Desktop**). Desktop page: Apps, Terminal, Browser, Files, Menu; Prev/Next win,
  Full screen, Float/tile, Close; Desk 1-5; Copy/Paste/Cut (Omarchy's terminal-aware
  Super+C/V/X), **Ctrl+A** (literally Ctrl+A: the installed Omarchy lacks upstream's
  Super+A); Key help; Rotate, Auto, Hide. Tap a modifier, then a key: it applies once and
  releases; several modifiers can be stacked; tap again to cancel.
- **Hide**: third key of the bottom row on letters/123/Fn, last key on Desktop. A manual
  Hide sticks until focus moves to another field. The bar's keyboard glyph toggles.
- **Home** shows when the workspace is empty or on request: clock, up to three recent
  windows, Omarchy's launcher app list (honouring Omarchy's hide rules).
- **Dock**: Home · Apps (Omarchy search) | Terminal · Browser (switch to newest window or
  start one; dot = running) | Open apps · Keyboard.
- **Apps run full screen**, one at a time (monocle layout, no gaps/borders; see
  [12-performance.md](12-performance.md)). The dock steps aside for a permanent 34 px
  handle: tap → dock peeks (tap away or 6 s), swipe up → Home.
- **Typing**: the dock hides while keys are up; the handle stays under the keys (swipe up
  → Home and keys drop; tap → keys drop and dock peeks).
- **Open apps**: every window newest first; tap to switch, × to close that one.

### Kid Mode

Start with **Kid Mode** at the top right of Home. First run asks for a parent PIN (at
least 4 digits, twice) and the apps to allow. Then: big tiles of allowed apps on a
`kids` workspace, no bar/dock/handle/Open apps, each app maximised (floating apps tiled
first), a yellow **Home** pill returns to the tiles. Parent: lock icon → PIN → *Choose
apps*, *Change PIN*, *Leave Kid Mode*. Five wrong PINs lock the pad for one minute.

Locks: any window that is not an allowed app is closed as it opens (the first window
after a tile tap is learned as that app's class, e.g. the YouTube web app →
`chrome-youtube.com__-Default`); the keyboard switches to `kidabc`/`kid123` (no
Super/Ctrl/Alt, no Fn/Desktop, only Hide), so no Omarchy shortcut can be typed;
`tablet-ui` refuses `apps/menu/terminal/browser/files/close/desktop`. State: flag file
`~hypr/.local/state/tablet-touch/kid-mode` (survives restarts) and
`~hypr/.config/omarchy/tablet-kids.json` (allow list, learned classes, salted MD5 PIN
hash).

Honest limits:
- The PIN hash is salted MD5 in a user-writable file. It keeps a child out, not an adult
  with file or SSH access.
- Enforcement lives in the user's shell session: a disallowed window is closed after it
  maps, not prevented. Notifications still pop up on top.
- The physical power button still opens Omarchy's power menu (shutdown/restart only).
- A Hyprland submap could not replace the kid keyboard pages: Omarchy's binds are all
  `submap_universal`, and an empty submap does not register.
- Not tested: a real reboot while in Kid Mode, portrait.

Forgotten PIN (from the desktop):

```sh
tools/tablet 'rm -f /home/hypr/.config/omarchy/tablet-kids.json /home/hypr/.local/state/tablet-touch/kid-mode'
tools/tablet 'rm -f /home/hypr/.local/state/omarchy/toggles/bar-off'   # only if the bar stays hidden
shell-plugin/restart-shell
tools/tablet 'cd /home/hypr && runuser -u hypr -- env XDG_RUNTIME_DIR=/run/user/1001 WAYLAND_DISPLAY=wayland-1 \
  /opt/omarchy-tablet/gpu/app-env /opt/omarchy-tablet/ui/tablet-ui keyboard-restart'
```

## Pitfalls and hard-won lessons

Keyboard / input:
- **One tap must give one letter.** A delayed touch release plus compositor key repeat
  produced repeats. Fixes, all needed: per-device Hyprland rule `repeat_rate = 0` for
  `hl-virtual-keyboard-wvkbd-mobintl` (so the executable name `wvkbd-mobintl` must not
  change), `--no-popup`, sending and flushing key-down before drawing feedback (CPU
  rendering delayed input), flushing key-up/modifiers before redraw, and dispatching
  pending Wayland events before `poll()` (a roundtrip inside a callback can queue events
  without making the fd readable, stranding a key-up).
- Stale releases: ignore touch-up/motion from a touch ID other than the active one;
  reset state on `wl_touch.cancel` and on hide.
- Forced shortcut modifiers (e.g. Super+F from the Desktop page) must be released
  explicitly and combined with latched modifiers with OR, not XOR (XOR left Super/Alt
  stuck or cancelled them).
- Fixed actions run `/opt/omarchy-tablet/ui/tablet-ui <name>` via `fork/execl` with a
  fixed argv; no key label ever becomes shell text.
- **Install the keyboard binary and `tablet-ui` together.** The helper uses
  SIGRTMIN+1 (Desktop page) and SIGRTMIN+2 (letters), which older binaries do not
  handle. SIGUSR1 hides, SIGUSR2 shows, SIGRTMIN toggles. Never signal a keyboard
  process right after starting it (handlers not installed yet).
- `tablet-ui start` is a leftover from the pre-Omarchy prototype (it starts Waybar with
  `/opt/omarchy-tablet/ui/waybar.json`); those Waybar/Wofi files are not included and the
  current session never calls it. Use `keyboard-start`.
- The very first prototype used a Wofi layer-shell launcher; typing failed until it
  was changed to a normal window. The precise layer-mode cause was never proven.
- fcitx5's DBus virtual-keyboard route was tried first (a bridge owning
  `org.fcitx.Fcitx5.VirtualKeyboard`): fcitx stays on `classicui` and never calls Show;
  disabling classicui leaves no UI. Abandoned for `waylandim` off + wvkbd `--auto`.
- `--auto` debounce: hiding immediately on focus loss unmapped/remapped the keyboard
  when moving between fields or apps, resizing the app twice (each a full software
  re-render). wvkbd now waits 180 ms (timerfd) and cancels if another field takes focus.
- The installed Omarchy 4.0.6 differs from the upstream checkout: `WidgetButton` has no
  `pressScale` (it was removed before activation) and there is no Super+A binding.

Shell plugin / Quickshell / QML:
- **Never run stock `omarchy-restart-shell` on this tablet.** It relaunches the shell
  from Hyprland's GPU (Zink) environment; Quickshell then maps layer surfaces that draw
  nothing (bar invisible, space still reserved). Use `shell-plugin/restart-shell`.
- `keepLoaded` plugins are not hot-replaced on file change (the old component stays
  cached): restart the shell after every plugin edit.
- Third-party plugins cannot rely on the host's `shell.appLibrary` facade (null, later
  revoked). The plugin reads `DesktopEntries` plus Omarchy's hide rules
  (`launcher.hides`, `hidden-entries.sh`) and launches with
  `uwsm-app -- gtk-launch <id>.desktop`, like AppLibrary.
- Nested `TapHandler`s lose to the parent's on touch: keep tap targets as
  non-overlapping siblings. A `ReleaseWithinBounds` tap blocks a sibling `DragHandler`;
  use `DragThreshold`. `DragHandler.translation` resets when the drag ends: record
  `activeTranslation` while active.
- Keep surfaces mapped. Mapping a new full-screen layer costs ~150 ms (llvmpipe first
  frame). Home, Open apps, dock and the peek catcher stay mapped and toggle content
  `visible` plus an input `mask: Region` (zero size = click-through). Touches pass
  through masked layers (verified by tapping between two tiled windows).
- The handle surface and its 34 px reservation are permanent; only its content
  shows/hides. Mapping/unmapping it resized the app below every time.
- Layer order: Home and Open apps are `Top`; the dock is `Overlay` so it stacks above
  them (it hides whenever wvkbd, also `Overlay`, is up).
- **Rotation**: after a screen size change, Quickshell surfaces kept a stale horizontal
  scale (handle squeezed to 1/1.6 in portrait, stretched 1.6× in landscape) until
  re-created. `TouchShell.qml` watches the screen size and unmaps every plugin surface
  (including Kid Mode's) for 250 ms, then maps them at the new size. An IPC
  "prepare rotate" hint from `tablet-orientation` gained only ~0.15 s and was removed.
  The whole screen settles ~0.7-1.0 s after rotation, mostly apps redrawing in software.

## Rollback

- Plugin: remove `{"id":"tablet.touch"}` from `plugins` in `~hypr/.config/omarchy/shell.json`
  (or restore it from `/opt/omarchy-tablet/backups/touch-shell-*`), then `shell-plugin/restart-shell`.
- Bar: restore `shell.json` and `bar/modules/TabletButton.qml` from
  `/opt/omarchy-tablet/backups/touch-ui-*`.
- Keyboard: restore `wvkbd-mobintl` and `tablet-ui` **together** from
  `/opt/omarchy-tablet/backups/touch-keyboard-*`, then restart only the keyboard
  (`tablet-ui keyboard-restart` as hypr, as in the Kid Mode recovery above).
- fcitx5: delete `~hypr/.config/systemd/user/omarchy-fcitx5.service.d/tablet.conf`, then
  `systemctl --user daemon-reload && systemctl --user restart omarchy-fcitx5` as hypr.
- None of this touches packaged Omarchy files, Hyprland Lua, the desktop service, GPU
  settings or the kernel.

## How it works

- **Keyboard**: wvkbd is a layer-shell client that sends keys through
  `zwp_virtual_keyboard_v1`. With `--auto` it also binds `zwp_input_method_v2`; the
  compositor activates it when a text-input-v3 field gains focus (foot, GTK and Chromium
  all use text-input-v3), which shows the keys. `tablet-ui keyboard-start` runs
  `wvkbd-mobintl -H 360 -L 260 --no-popup --auto --hidden --fn 'DejaVu Sans 20'` with the
  tablet (or kid) page set. The patch adds an `Action` key type, the six tablet/kid
  layouts, named-page signals, the input fixes above and the auto-hide timer.
- **Shell plugin**: a `menu`-kind, `keepLoaded` plugin inside the existing
  `omarchy-shell` Quickshell process. It talks to Hyprland via `hyprctl` and
  `Quickshell.Hyprland`, tracks Hyprland `openlayer`/`closelayer` events for the
  `wvkbd` namespace (plus one `hyprctl layers -j` probe at load) to know whether the keys are up, and
  calls `tablet-ui` for keyboard control.
- **Why Option A**: three directions (A Home & Dock, B App Rail, C Workspace Deck) were
  mocked across the same 18 surfaces in `shell-plugin/design/omarchy-touch-review.html`;
  the user picked A and two refinement passes followed (apps fill the screen, dock steps
  aside behind a handle, tap-to-type with one Hide control). See
  `shell-plugin/design/README.md`. The mock's gesture text-editing menu was not built
  natively; that depends on each app.
