# 12 - Performance and responsiveness

Measured responsiveness work on the native Omarchy session, 2026-10-29: why the UI felt
slow, what was changed, before/after numbers, and the tools used to measure it. The
touch shell itself is in [10-touch-ui.md](10-touch-ui.md); clocks and "can we
overclock" are also summarised in [11-power-battery.md](11-power-battery.md).

Files in this repo:

| Path | What |
|---|---|
| `perf/tablet-touch-boost`, `perf/tablet-touch-boost.service` | Raise CPU minimum frequency while the screen is touched |
| `perf/tablet-chromium-warm.service` | User service keeping one windowless Chromium running |
| `perf/qa-latency.py` | End-to-end latency on the real display (kmsgrab) |
| `perf/measure-frame.py`, `perf/measure-open.py` | Touch → shell frame / layer-map timers |
| `perf/chromium-bench/` | CDP benchmarks of a throwaway Chromium (`bench.mjs`, `sites.mjs`, `page.html`, `ctl.sh`, A/B drivers) |

## Status

| Item | State |
|---|---|
| Apps (incl. Chromium) render on the CPU (llvmpipe); compositor on the GPU (Zink) | By design, current state |
| Shell surfaces kept mapped: tap → first frame under ~10 ms (was ~150 ms) | Done, measured |
| Touch CPU boost service | Done, measured (small gain) |
| Resize cascades on launch/typing removed (gaps/borders 0, permanent handle, keyboard debounce) | Done, measured |
| Warm Chromium: browser/YouTube open in ~0.9 s instead of ~2.4 s | Done, measured |
| YouTube web app on the mobile site | Done; LCP ~1.3 s warm vs 3.4-3.6 s desktop site |
| Window open/close animations | Off (reverted on a wrong guess; could be retried, untested) |
| Heavy pages (desktop YouTube) | Not fixable by settings: single-thread JavaScript bound |
| Native-resolution fullscreen video | Still drops frames (~17 % on 1080p main content; 7-11 % on a 1080p30 stress) |
| Adapted scripts in this repo (`chromium-bench/*.sh`) | Not re-run in this form (SSH via `tools/tablet`) |

## Prerequisites

- The session from the earlier docs: GPU compositor via Zink/PanVK with clients wrapped in
  `/opt/omarchy-tablet/gpu/app-env` (llvmpipe), see [`graphics/`](../graphics/); Chromium
  with the tablet's `chromium-flags.conf` and GPU launcher, see [`video/`](../video/);
  the touch shell from [10-touch-ui.md](10-touch-ui.md).
- On the tablet for `qa-latency.py`: `ffmpeg` with `kmsgrab`, root.
- On the desktop for `chromium-bench`: Node.js with built-in `fetch`/`WebSocket`
  (Node 22 or newer).

## Reproduce

### 1. Touch boost

```sh
tools/tablet 'install -d -m 755 /opt/omarchy-tablet/perf && cat > /opt/omarchy-tablet/perf/tablet-touch-boost &&
  chmod 755 /opt/omarchy-tablet/perf/tablet-touch-boost' < perf/tablet-touch-boost
tools/tablet 'cat > /etc/systemd/system/tablet-touch-boost.service' < perf/tablet-touch-boost.service
tools/tablet 'systemctl daemon-reload && systemctl enable --now tablet-touch-boost'
```

What it does: Android's power HAL raises clocks on touch; here schedutil let idle
cores sit at 500/725 MHz, so the first frames of an interaction rendered while clocks
ramped. The service reads `mtk-tpd` without grabbing it and, while the screen is touched
and 1.2 s after, sets `scaling_min_freq` to 1.5 GHz (policy0, little) and 1.8 GHz
(policy6, big); it restores the original minimums afterwards and on stop.
Verify: `tools/tablet 'cat /sys/devices/system/cpu/cpufreq/policy{0,6}/scaling_min_freq'`
while touching vs idle.

### 2. Warm Chromium

```sh
tools/tablet "runuser -u hypr -- sh -c 'mkdir -p ~/.config/systemd/user &&
  cat > ~/.config/systemd/user/tablet-chromium-warm.service'" < perf/tablet-chromium-warm.service
tools/tablet 'runuser -u hypr -- env XDG_RUNTIME_DIR=/run/user/1001 systemctl --user daemon-reload &&
  runuser -u hypr -- env XDG_RUNTIME_DIR=/run/user/1001 systemctl --user enable --now tablet-chromium-warm.service'
```

`chromium --no-startup-window --keep-alive-for-test` (same profile and flags) stays
running without windows (~120 MB); later `chromium` / web-app launches hand off to it.
`OOMScoreAdjust=300` lets the kernel reclaim it first under memory pressure.

### 3. Measure end-to-end latency

```sh
tools/tablet 'install -d -m 700 /opt/omarchy-tablet/diagnostics &&
  cat > /opt/omarchy-tablet/diagnostics/qa-latency.py && chmod 700 /opt/omarchy-tablet/diagnostics/qa-latency.py' \
  < perf/qa-latency.py
tools/tablet 'python3 /opt/omarchy-tablet/diagnostics/qa-latency.py 3 foot chromium-warm-over-app keyboard'
```

`qa-latency.py [RUNS] [SCENARIO...]` captures the real display with ffmpeg `kmsgrab`
(~50-60 Hz, wall-clock timestamps, downscaled grey frames), runs the action as `hypr` in
the shell's environment (Home taps go through `uwsm-app -- gtk-launch` like the plugin),
and prints **first** (action → first changed frame) and **settle** (→ last change,
0.8 s quiet) in ms, then undoes the action. Scenarios: `home/switcher/peek-over-app`,
`keyboard`, `keyboard-sig`, `foot`, `files`, `chromium`, `calc`, `youtube`,
`chromium-warm-over-app`, `youtube-warm-over-app`, `foot-direct`, `foot-uwsm`.
`*-over-app` open Chromium first (`QA_BACKDROP` overrides the desktop id). Raw results:
`/tmp/qa-latency.json` on the tablet.

**Gotcha:** ffmpeg must run with `-fps_mode passthrough`. Without it ffmpeg re-timed
frames (duplicate/drop), stdout frames no longer paired 1:1 with `showinfo` timestamps,
and timings drifted ~5 %.

The two older timers need `touch-tap.py` installed (see [10-touch-ui.md](10-touch-ui.md))
and tap the dock's Open apps button at panel (1118, 737) in landscape:
`measure-frame.py RUNS` (touch-up → the plugin's own `switcherFrameAt` probe) and
`measure-open.py RUNS` (→ Hyprland `openlayer>>tablet-switcher` event; includes ~100 ms
tool overhead). Copy them to `/opt/omarchy-tablet/diagnostics/` the same way.

### 4. Browser benchmarks (optional)

```sh
perf/chromium-bench/run.sh        # flush vs always-finish GPU-launcher mode, page.html
perf/chromium-bench/gpu-ab.sh     # llvmpipe GL vs --disable-gpu
perf/chromium-bench/sched-ab.sh   # EAS on/off, sched_pelt_multiplier 1/4 (restores originals)
perf/chromium-bench/ddr-ab.sh     # automatic DDR vs forced top DVFSRC level (restores 255)
```

Each stages `ctl.sh` and `page.html` to `/tmp/bench`, opens an SSH tunnel to port 9333,
starts a throwaway Chromium (own profile, same GPU launcher, `--remote-debugging-port=9333`)
via `ctl.sh`, and runs `bench.mjs` (navigation → first frame, rAF fps while scrolling 5 s)
and/or `sites.mjs URL...` (TTFB, FCP, LCP, long tasks; cold and warm). `ctl.sh` depends on
`/opt/omarchy-tablet/video/bin/chromium-gpu-launcher` from [`video/`](../video/).

## Findings

### Resize cascades (the main cause of "sluggish")

Hyprland shows a window only once the app has rendered at the size it was told, and
every resize is a full software re-render. A `WAYLAND_DEBUG` trace of one terminal launch
showed six configures:

1. Hyprland's first size guess ignored the 2 px border (1260x732 → 1256x728);
2. the handle mapped and reserved 34 px (→ 694);
3. the keyboard rose (→ 434) and, because the handle hid while typing, the 34 px came
   back (→ 468);
4. switching between two text apps unmapped and remapped the keyboard (two more).

Every peek, Open apps and Home over an app also mapped/unmapped the handle, resizing
the app twice; with Chromium each costs hundreds of ms.

Fixes:

| Change | Where |
|---|---|
| Gaps and borders 0 (apps are full screen one at a time), so the first size guess is right | appended to `~hypr/.config/hypr/looknfeel.lua` |
| Handle always mapped with a constant 34 px reservation; only content/input toggles. While typing it sits under the keys | `shell-plugin/plugin/tablet.touch/TouchShell.qml` |
| wvkbd `--auto` hides 180 ms after a field loses focus, cancelled if another field takes it | `touch-ui/wvkbd/tablet-wvkbd-v0.20.patch` (`main.c`, timerfd) |
| Windowless Chromium kept warm | `perf/tablet-chromium-warm.service` |

After: one configure at the right size plus one for the keyboard; six focus switches
between two terminals caused zero keyboard close/open events; closing the last text app
still hides the keys.

### Before / after (3 runs each, ms, same session, `qa-latency.py`)

| Scenario | Before first / settle | After first / settle |
|---|---|---|
| Terminal (foot) from Home | 433-471 / 771-820 | 431-454 / 482-602 |
| Chromium, cold | 2531-2566 / 3628-3799 | 2328-2358 / 3147-3252 |
| Chromium, warm service | - | 918-939 / 1808-1951 (first after boot 1741 / 2744) |
| YouTube web app, warm service | (cold ≈ Chromium cold) | 842-877 first (settle = page load) |
| Files (warm) | 505-538 / 623-689 | 501-547 / 602-631 |
| Peek over Chromium (keys up) | nothing shown | 185-241 / 219-474 |
| Open apps / Home over Chromium | ~200-230 | ~210-250 |
| Keyboard show (helper) | 233-292 / 393-435 | 219-293 / 373-449 |

Omacalc (~1.1 s) is its own startup and was not changed.

Earlier the same day:

| Change | Effect |
|---|---|
| Shell surfaces kept mapped (Home, Open apps, dock, peek catcher toggle content `visible` + input `mask`) | Tap → first frame of Open apps ~150 ms → under ~10 ms (6 runs each, `measure-frame.py`) |
| Touch boost | Old surface path: median 317 → 296 ms, worst 377 → 306 ms (`measure-open.py`, incl. ~100 ms tool overhead) |

### Baseline checks

- Compositor adds no delay: a client commit reaches the screen in ~38 ms (two vblanks
  plus capture).
- Idle: CPU ~97 % idle; journald receives ~900 vendor kernel lines/min (~0.4 % of a core,
  left alone); the rootfs UFS LUN runtime-suspends after 2 s, a cold read costs ~2-3 ms
  vs 0.1 ms (negligible); schedutil `rate_limit_us` is 1000.

### Chromium artifacts and animations

- Window animations were turned on (pop-in/out ~250 ms, fades, workspace slide; compositor
  on Zink used ~8 % of one core) and then reverted when the user saw artifacts while
  Chromium loaded. That guess was wrong.
- A kmsgrab capture found the real causes: (1) side-by-side tiling resized the visible
  Chromium window, which then showed cropped, striped stale frames for ~0.5 s; fixed with
  Hyprland's `monocle` layout (one app at a time, as Option A intends; floating windows
  unaffected); (2) partly rasterised frames reaching the compositor through the video
  loader's flush shortcut; fixed with always-finish (see [`video/`](../video/)).
- Currently disabled in `looknfeel.lua`: `windows`, `windowsIn`, `windowsOut`,
  `windowsMove`, `fadeIn`, `fadeOut`, `workspaces`; only `layersIn` (shell/menu fades)
  stays on. Window animations could be retried now; untested. The keyboard layer stays
  `no_anim`.
- A grim burst capture (~350 ms per frame) cannot see a 250 ms animation; use kmsgrab.

### Page loading

| Variable tested | Result |
|---|---|
| Always-finish vs flush | Same: ~30 fps scrolling, 140-230 ms to first frame. Not the cause; always-finish kept |
| EAS on/off, `sched_pelt_multiplier` 1/4 | No difference (YouTube LCP 3.1-3.5 s in every case); restored 1/1 |
| `--disable-gpu` (software compositing) | FCP ~300 ms earlier on YouTube, but scrolling 30 → 24 fps and no hardware video. Rejected |
| DDR forced to top level (3200 MHz held) | No difference: YouTube LCP 2.8-3.5 s, Wikipedia ~0.5-1.1 s, scroll 30 fps |
| Content | YouTube desktop LCP 3.4-3.6 s (~2.8 s of long JS tasks); mobile site 1.2-1.4 s (~0.5 s); YouTube Kids 1.2-1.8 s |

Conclusion: page speed is limited by single-thread speed (Cortex-A76 at 2.2 GHz) on
heavy JavaScript, not by clocks, memory, scheduling or the browser graphics path.
Lighter content and blocking ad scripts help. Change made: the YouTube web app
(`~hypr/.local/share/applications/YouTube.desktop`) opens
`https://m.youtube.com/?app=m&persist_app=1`; warm loads paint in ~1.3 s (first visit
4.3 s while cookies were set). Side effect: `persist_app` is a cookie, so youtube.com in
the normal browser profile also turns mobile. Kid Mode re-learns the web app's window
class on its first tile tap after this change.

### Why apps stayed on llvmpipe (until 2026-10-31)

Superseded: since 2026-10-31 the shell, GTK 4/Qt apps and Chromium render on the Mali
GPU ([08-gpu.md](08-gpu.md#applications-and-chromium-on-the-gpu-2026-10-31)); the
measurements in this document were taken before that. At the time:
the compositor ran on the Mali GPU through Zink/PanVK, but experimental browser Mali
rendering failed synchronisation/scanout checks, so Chromium and other clients kept
llvmpipe (eight workers). H264 decoding is hardware (Chromium `V4L2VideoDecoder`); the
remaining video cost is Chromium rasterising and compositing on the CPU. Details:
[`graphics/`](../graphics/) and [`video/`](../video/).

## Pitfalls

- Do not restart the shell with stock `omarchy-restart-shell` (black surfaces from the
  compositor's environment); use `shell-plugin/restart-shell`.
- Measure with kmsgrab and `-fps_mode passthrough`, not grim bursts.
- Animations, scheduler knobs, DDR forcing and `--disable-gpu` were all tried and did not
  address the real costs (resizes and site JavaScript).

## Gaps

The final `~hypr/.config/hypr/looknfeel.lua` (animations, `monocle` layout, gaps/borders 0)
is [`omarchy/staged/looknfeel.lua`](../omarchy/staged/looknfeel.lua). The edited
`YouTube.desktop` web-app entry only changed its `Exec=` line to
`Exec=omarchy-launch-webapp https://m.youtube.com/?app=m&persist_app=1`. Backups on the
original device were
`/opt/omarchy-tablet/backups/looknfeel.lua.pre-{artifact,maximize,nogaps}-*` and
`/opt/omarchy-tablet/backups/YouTube.desktop.*`.

## Rollback

```sh
tools/tablet 'systemctl disable --now tablet-touch-boost'        # restores the original minimum frequencies on stop
tools/tablet 'runuser -u hypr -- env XDG_RUNTIME_DIR=/run/user/1001 systemctl --user disable --now tablet-chromium-warm'
```

Restore `looknfeel.lua` from its backup and `hyprctl reload`; restore the plugin from
`/opt/omarchy-tablet/backups/touch-shell-*` and run `shell-plugin/restart-shell`.
