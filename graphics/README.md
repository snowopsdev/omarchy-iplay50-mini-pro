# graphics/

Sources for the GPU-composited Hyprland desktop. Full instructions, status and
lessons: [`../docs/08-gpu.md`](../docs/08-gpu.md).

| Path in this repo | Installed on the tablet as | Purpose |
|---|---|---|
| `shim/drm-no-cursor.c` | `/opt/omarchy-tablet/drm-no-cursor.so` | Refuses the hardware-cursor ioctls that reset the tablet |
| `shim/aq-software-primary.c` | `/opt/omarchy-tablet/aq-software-primary.so` | Software (llvmpipe) profile: glFlush→glFinish for Hyprland, skip Aquamarine's unused secondary renderer |
| `shim/aq-primary-zink.c` | `/opt/omarchy-tablet/gpu/aq-primary-zink.so` | Same two fixes, also accepting `GALLIUM_DRIVER=zink` + `AQ_TABLET_ZINK_PRIMARY=1` |
| `shim/egl-shim.c`, `shim/drm-trace.c`, `shim/build.sh` | not installed | libmali experiment helper and DRM ioctl tracer (diagnosis only) |
| `profile/start-hyprland-gpu` | `/opt/omarchy-tablet/gpu/start-hyprland` | Desktop unit's compositor wrapper; sources the GPU environment |
| `profile/tablet-gpu-trial-env` | `/opt/omarchy-tablet/gpu/tablet-gpu-trial-env` | Zink/PanVK variables, only when a GPU marker exists |
| `profile/app-env` | `/opt/omarchy-tablet/gpu/app-env` | Resets apps (shell, terminal, browser) to llvmpipe |
| `profile/omarchy-autostart.lua` | `/home/hypr/.config/default/hypr/autostart.lua` | Omarchy 4.0.6 default autostart, each command wrapped in `app-env` |
| `profile/end-gpu-trial` | `/opt/omarchy-tablet/gpu/end-gpu-trial` | Rollback: remove both GPU markers, restart desktop |
| `profile/prepare-gpu-boot-trial`, `profile/gpu-boot-trial.conf` | `/opt/omarchy-tablet/gpu/prepare-gpu-boot-trial` + a drop-in for `omarchy-desktop.service` | One-boot GPU trial |
| `profile/99-tablet-gpu.rules` | `/etc/udev/rules.d/99-tablet-gpu.rules` | `system` dma-heap → root:render 0660 |
| `panvk/fetch-panvk-source.sh` | (host or tablet) | Rebuilds the exact PanVK source from pinned upstream + patches |
| `panvk/build-panvk-tablet.sh` | run on the tablet | Meson/Ninja build into `/opt/omarchy-tablet/gpu/panvk-fourfect` |
| `panvk/patches/fourfect-kbase-only-build.patch` | — | Our only change to the PanVK source |
| `panvk/test-panvk-tablet.sh` | `/opt/omarchy-tablet/diagnostics/gpu/` | Runs one isolated probe (enumerate/compute/gles/gbm) |
| `tests/` | `/opt/omarchy-tablet/diagnostics/gpu/` (built there) | `gpu-smoke` (Vulkan compute), `egl-smoke` (GLES/xfb/GBM), `framebench`, `kmstest`, `test-sw.sh`, `hypr-test.lua` |
| `browser-gpu/` | not installed | Rejected Chromium GPU trials (kept for reference) |
| `historical/` | not installed | Abandoned libmali route (test script, Hyprland EGL patch, dma-buf exporter build) |

Nothing here was re-tested after being copied into this repository. Two scripts
were edited for the new layout (`shim/build.sh` linker path,
`historical/dma-exporter/build.sh` inputs) and `panvk/build-panvk-tablet.sh`
gained a `PANVK_SRC` override; `panvk/fetch-panvk-source.sh` is new.
