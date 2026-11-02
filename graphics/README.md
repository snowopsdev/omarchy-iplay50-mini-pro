# graphics/

Sources for the GPU-composited Hyprland desktop, and (since 2026-10-31) for
GPU rendering of the shell, applications and Chromium. Full instructions, status and
lessons: [`../docs/08-gpu.md`](../docs/08-gpu.md).

| Path in this repo | Installed on the tablet as | Purpose |
|---|---|---|
| `shim/drm-no-cursor.c` | `/opt/omarchy-tablet/drm-no-cursor.so` | Refuses the hardware-cursor ioctls that reset the tablet |
| `shim/aq-software-primary.c` | `/opt/omarchy-tablet/aq-software-primary.so` | Software (llvmpipe) profile: glFlush→glFinish for Hyprland, skip Aquamarine's unused secondary renderer |
| `shim/aq-primary-zink.c` | `/opt/omarchy-tablet/gpu/aq-primary-zink.so` | Same two fixes, also accepting `GALLIUM_DRIVER=zink` + `AQ_TABLET_ZINK_PRIMARY=1` |
| `shim/zink-kopper-hw.c` | `/opt/omarchy-tablet/gpu/zink-kopper-hw.so` | Hides `LIBGL_ALWAYS_SOFTWARE` from Zink only, so Wayland GL windows use Zink's Vulkan swapchain on the Mali GPU |
| `shim/egl-shim.c`, `shim/drm-trace.c`, `shim/build.sh` | not installed | libmali experiment helper and DRM ioctl tracer (diagnosis only) |
| `profile/start-hyprland-gpu` | `/opt/omarchy-tablet/gpu/start-hyprland` | Desktop unit's compositor wrapper; sources the GPU environment |
| `profile/tablet-gpu-trial-env` | `/opt/omarchy-tablet/gpu/tablet-gpu-trial-env` | Zink/PanVK variables, only when a GPU marker exists |
| `profile/app-env` | `/opt/omarchy-tablet/gpu/app-env` | Graphics profile for the shell and apps: `gpu-app-env` if `gpu/apps-gpu` exists, else llvmpipe |
| `profile/gpu-app-env` | `/opt/omarchy-tablet/gpu/gpu-app-env` | GPU profile for apps (Zink/kopper on the `panvk-apps` build, dma-heap swapchain) |
| `profile/omarchy-autostart.lua` | `/home/hypr/.config/default/hypr/autostart.lua` | Omarchy 4.0.6 default autostart, each command wrapped in `app-env`; clears graphics variables from the user manager before importing |
| `profile/mpv.conf` | `/home/hypr/.config/mpv/mpv.conf` | `gpu-api=opengl` (mpv's Vulkan output crashes on PanVK) |
| `profile/end-gpu-trial` | `/opt/omarchy-tablet/gpu/end-gpu-trial` | Rollback: remove both GPU markers, restart desktop |
| `profile/prepare-gpu-boot-trial`, `profile/gpu-boot-trial.conf` | `/opt/omarchy-tablet/gpu/prepare-gpu-boot-trial` + a drop-in for `omarchy-desktop.service` | One-boot GPU trial |
| `profile/99-tablet-gpu.rules` | `/etc/udev/rules.d/99-tablet-gpu.rules` | `system` dma-heap → root:render 0660 |
| `panvk/fetch-panvk-source.sh` | (host or tablet) | Rebuilds the exact PanVK source from pinned upstream + patches |
| `panvk/build-panvk-tablet.sh` | run on the tablet | Meson/Ninja build into `/opt/omarchy-tablet/gpu/panvk-fourfect` |
| `panvk/patches/fourfect-kbase-only-build.patch` | — | Build fix, in both PanVK builds |
| `panvk/patches/panvk-kbase-signalled-export.patch` | — | Apps/browser build only (`/opt/omarchy-tablet/gpu/panvk-apps`): SYNC_FD export of finished work returns a signalled `sync_file` instead of -1 |
| `panvk/test-panvk-tablet.sh` | `/opt/omarchy-tablet/diagnostics/gpu/` | Runs one isolated probe (enumerate/compute/gles/gbm) |
| `tests/` | `/opt/omarchy-tablet/diagnostics/gpu/` (built there) | `gpu-smoke` (Vulkan compute), `egl-smoke` (GLES/xfb/GBM), `egl-fence-test` (native fence export), `egl-wayland-test` (GL window reaches the screen), `buffer-bench` (fill/copy/rotation into native and linear dma-buf targets, cost of many small render passes), `kbase-job-times.sh` (Mali job time per slot, kprobes), `framebench`, `kmstest`, `test-sw.sh`, `hypr-test.lua` |
| `browser-gpu/chromium-gpu-process-launcher` | `/opt/omarchy-tablet/gpu/browser/chromium-gpu-process-launcher` | Chromium `--gpu-launcher`: GPU process on the Mali GPU, plus the hardware video bridge |
| `browser-gpu/chromium-gpu-trial`, `cdp.mjs`, `cdp-trace.mjs`, `playback.html` | not installed | Isolated trial launcher (`TABLET_CHROMIUM_ICD` selects the driver), DevTools helper, DevTools trace summary (time per GPU/Viz event), playback measurement page |
| `electron-gpu/electron-gpu-process-launcher`, `electron-gpu-run` | `/opt/omarchy-tablet/gpu/electron/` | Electron apps on the Mali GPU: `--gpu-launcher` environment (no video bridge) and the flags wrapper |
| `electron-gpu/obsidian` | `/usr/local/bin/obsidian` | Starts Obsidian through `electron-gpu-run` |
| other `browser-gpu/` files | not installed | Rejected Chromium GPU trials (kept for reference) |
| `historical/` | not installed | Abandoned libmali route (test script, Hyprland EGL patch, dma-buf exporter build) |

The 2026-10-31 files (`gpu-app-env`, the new `app-env` and autostart module,
`zink-kopper-hw.c`, `chromium-gpu-process-launcher`, the signalled-export
patch and the two tests) are byte-identical to what runs on the project's
tablet (the patch was also checked with `git apply --check` against the
FourFect tree); `fetch-panvk-source.sh`'s `PANVK_APPS=1` option was not run.
Apart from that, nothing here was re-tested after being copied into this repository. Two scripts
were edited for the new layout (`shim/build.sh` linker path,
`historical/dma-exporter/build.sh` inputs) and `panvk/build-panvk-tablet.sh`
gained a `PANVK_SRC` override; `panvk/fetch-panvk-source.sh` is new.
