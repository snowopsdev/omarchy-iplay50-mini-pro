# 08 — GPU: Hyprland compositor on the Mali-G57 (Zink + PanVK/kbase)

The desktop compositor (Hyprland) renders on the GPU through Mesa's Zink
(OpenGL ES on Vulkan) and a privately built PanVK Vulkan driver that talks to
the tablet's own vendor `mali_kbase` kernel driver. Every application,
including the Omarchy shell, terminals and Chromium, stays on the software
renderer (llvmpipe). Source files: [`graphics/`](../graphics/README.md).
Work dates: 2026-10-28 (bring-up) and 2026-10-29 (follow-ups).

## Status

| Item | Status | Notes |
|---|---|---|
| Hyprland compositing on Mali-G57 MC2 via Zink + PanVK/kbase | **Works** | User physically confirmed Apps, Term, Close and typing are responsive and flicker-free. Passed a one-shot GPU boot and a second normal boot with the persistent profile. No GPU faults logged. |
| Vulkan compute (1024 elements, checked against CPU) | Works | `gpu-smoke`, as the unprivileged desktop user |
| GLES shader draw + readback, transform feedback, GBM linear dma-buf export, mediatek-drm PRIME import, EGL dma-buf import | Works | `egl-smoke` / `egl-smoke gbm`; GLES 3.1 reported, no version overrides |
| Applications (shell, Foot, Chromium, …) on GPU | **Not used** (by design) | Kept on llvmpipe; see "Why apps stay on llvmpipe" |
| Chromium GPU rendering (ANGLE GLES-EGL, ANGLE Vulkan, X11 GLES) | **Not working** | Fence export failure / scanout allocation failure / no DRI3; all rejected |
| Hardware cursor plane | **Not usable** | The vendor cursor ioctl resets the tablet; a shim refuses it, cursors are software |
| Arm libmali (MediaTek Genio r32p0 builds) | **Not working** | Both builds reject this GPU (GPU ID 0x9093 r0p1); abandoned |
| Vulkan conformance | Not conformant | PanVK prints "not a conformant Vulkan implementation, testing use only" |
| Performance | Partly measured | One 4-second 400x400 redraw check: 45.22 fps; not a controlled comparison with the CPU profile. Compositor adds no measurable delay: a client commit reaches the screen in ~38 ms (2 vblanks + capture), see [12-performance.md](12-performance.md). |
| Long-term stability, suspend/resume on GPU | Unverified | |

## How it works

```
omarchy-desktop.service
  └─ uwsm … /opt/omarchy-tablet/gpu/start-hyprland            (graphics/profile/start-hyprland-gpu)
       sources /opt/omarchy-tablet/gpu/tablet-gpu-trial-env   (only if a GPU marker exists)
       └─ /usr/bin/start-hyprland → Hyprland 0.56.2
            GLES 3.0 context ← Mesa Zink (system Mesa 26.2.5) ← PanVK (private, /opt) ← /dev/mali0 (vendor kbase r32p1)
            buffers: GBM kms_swrast allocation on /dev/dri/card0 (mediatek-drm, no render node)
            LD_PRELOAD: drm-no-cursor.so + aq-primary-zink.so
       autostart: every app command is wrapped in /opt/omarchy-tablet/gpu/app-env → llvmpipe
```

- **GPU and kernel.** Mali-G57 2 cores r0p1 (GPU info 0x9093). The kernel
  driver is the vendor `mali_kbase` `r32p1-01eac0` (UK version 11.31) on the
  stock Android GKI 5.10.149 kernel. The display is `mediatek-drm` `card0`
  with **no DRM render node**. Upstream Mesa has no kbase backend; it only
  supports the upstream panfrost/panthor kernel drivers.
- **PanVK on kbase.** A Mesa fork adds a kbase backend (`kbase_kmod.c`) to
  PanVK. It is Vulkan-only; Gallium OpenGL on kbase is deliberately gated off
  in the fork, so GLES comes from Zink running on this Vulkan driver. The
  driver is installed in a private prefix and selected only for the compositor
  with `VK_DRIVER_FILES`. PanVK enumerates as Mali-G57 MC2, vendor 0x13b5,
  device 0x90930010.
- **Why the FourFect variant.** With the first fork (yo-www/panvk) unmodified
  Zink exposed only GLES2, because Zink's feature table disables transform
  feedback for Mali arch 9 and Mesa needs it for GLES3. The FourFect patch set
  implements arch-9 transform feedback (so GLES 3.1 is real, not a forced
  version string) and moves kbase `MEM_EXEC_INIT` before `JIT_INIT` (the
  first build logged EPERM for that call). Hyprland uses its supported GLES 3.0
  fallback on the 3.1 context.
- **Buffer allocation.** `GBM_ALWAYS_SOFTWARE=1` makes Mesa's GBM use
  `kms_swrast` (KMS dumb buffers on `card0`), while `GALLIUM_DRIVER=zink`
  renders on the GPU. The `system` dma-heap is opened to the `render` group so
  the desktop user can allocate shareable buffers (`99-tablet-gpu.rules`).
  `/dev/mali0` was already world-accessible (`crw-rw-rw-`) and was not changed.
- **Markers.** `tablet-gpu-trial-env` exports the GPU variables only if
  `/run/tablet-gpu-trial` (volatile, disappears at reboot) or
  `/opt/omarchy-tablet/gpu/enabled` (persistent) exists. Otherwise it unsets
  them and the compositor runs on the software profile from Omarchy's
  `20-tablet` UWSM environment (see `omarchy/uwsm/20-tablet` and
  [04-omarchy.md](04-omarchy.md)).

### The three compositor shims and why each exists

All three are small `LD_PRELOAD` libraries in `graphics/shim/`.

1. **`drm-no-cursor.so` — mandatory, GPU or not.** The vendor
   `DRM_IOCTL_MODE_CURSOR` resets the tablet, even when clearing an unused
   cursor. A synchronous ioctl trace (`drm-trace.c`) ended with a successful
   `SETCRTC` followed by an unmatched entry to `0xc01c64a3`
   (`DRM_IOCTL_MODE_CURSOR`), then the reset; earlier hypotheses (battery,
   idle states, touch) did not hold up. The shim returns success for a "disable
   cursor" request with no buffer and `EOPNOTSUPP` for anything else
   (`MODE_CURSOR` and `MODE_CURSOR2`), so Hyprland falls back to software
   cursors. Keep `cursor.no_hardware_cursors = true` as well.
2. **`aq-software-primary.so` / `aq-primary-zink.so` — skip the unused
   secondary renderer.** Because the primary DRM node has no EGL device,
   Aquamarine 0.15.1 retried creating its (unused) secondary blit renderer
   after every commit. The shim interposes
   `CDRMBackend::updateSecondaryRendererState` and returns early when
   `shouldBlit()` is false, but only for an explicitly selected primary:
   `AQ_TABLET_SOFTWARE_PRIMARY=1` with `GALLIUM_DRIVER=llvmpipe`, or (the
   `-zink` variant) `AQ_TABLET_ZINK_PRIMARY=1` with `GALLIUM_DRIVER=zink`.
   Real secondary GPUs keep the packaged behaviour. No C++ object layouts are
   touched; both methods are exported by the installed library.
3. **glFinish instead of glFlush for Hyprland (same two libraries).** Hyprland
   0.56.2 decides whether it is software-rendering from the DRM driver name;
   with the display-only `mediatek` node it misclassifies llvmpipe and skips
   its llvmpipe `glFinish` path, so partly rasterised frames reached the
   screen (partial-window flicker). With `TABLET_FINISH_SOFTWARE_FRAMES=1` and
   process name `Hyprland`, the shim's `glFlush` calls the real `glFinish`.
   The user confirmed the flicker was gone. `tablet-gpu-trial-env` does not
   unset `TABLET_FINISH_SOFTWARE_FRAMES`, so (reading the scripts) the
   substitution also stays active for the Zink compositor.

`egl-shim.c` (stub `eglDebugMessageControlKHR`, `gbm_bo_create_with_modifiers2`
fallback) only served the abandoned libmali route; `drm-trace.c` is the
diagnostic tracer used to find the cursor reset.

### Why apps stay on llvmpipe

- Omarchy's packaged autostart imports **all** compositor variables into the
  systemd user manager and launches the shell directly. That first propagated
  the GPU variables to every app and left the bar blank. The replacement
  `omarchy-autostart.lua` (installed as the user module
  `/home/hypr/.config/default/hypr/autostart.lua`, which Omarchy's bootstrap
  sources ahead of the packaged default) keeps the packaged startup actions
  but runs each through `app-env`, which unsets the Vulkan/Zink variables,
  exports `GALLIUM_DRIVER=llvmpipe LIBGL_ALWAYS_SOFTWARE=1
  GBM_ALWAYS_SOFTWARE=1`, the software preloads and
  `UWSM_APP_UNIT_TYPE=service`, *before* the environment import. No file
  under `/usr/share/omarchy` is changed. Review this module against
  `/usr/share/omarchy/default/hypr/autostart.lua` after Omarchy updates.
  (This is a different file from the user's own `~/.config/hypr/autostart.lua`
  shipped in `omarchy/staged/`.)
- Chromium on the GPU failed (next section). Sandboxed GPU clients were also
  a concern: the PanVK-kbase authors document a kbase kernel panic when a GPU
  context is created from a PID namespace on a Pixel r54p3 kernel
  ([funnymdzz/mali_fxxker](https://github.com/funnymdzz/mali_fxxker)).
  On this tablet, bounded compute/GLES/GBM probes inside a private PID
  namespace later passed (`browser-gpu/pidns-probe`); that covers those
  probes only, not long-running sandboxed apps.

### Chromium on the GPU: what was tried and why it failed

Chromium 153.0.8010.36 (Arch Linux ARM). All trials used separate profiles
and launchers in `graphics/browser-gpu/` and `video/trials/`; none became the
normal launcher, and the compositor's driver was never replaced.

- **ANGLE GLES-EGL over Zink/PanVK** initialised real Mali compositing and
  rasterisation, but real pages made `eglDupNativeFenceFDANDROID` return -1
  and the GPU process exited. A simple local clip first passed, which hid the
  failure. Cause (source-level): PanVK's CPU-side sync-fd export can wait and
  return -1, which is a legal "already signalled" Vulkan result, but Mesa EGL
  treats the missing fd as a native-fence failure and Chromium's Ozone image
  `EndAccess` requires the fence. CPU raster, zero-copy,
  `--flush_before_create_fence` and filtering the EGL extension string did
  not make real YouTube reliable.
- **Browser-only ICD with sync-fd extensions hidden**
  (`browser-gpu/panvk-no-syncfd-extension-advertisement.patch`, used with
  `PANVK_KBASE_NO_SYNCFD=1`): compute passed, but GBM drawing segfaulted and
  Chromium's GPU process crashed. **Rejected; do not deploy.**
- **ANGLE Vulkan** with ANGLE's own `supportsAndroidNativeFenceSync` disabled
  (`browser-gpu/chromium-angle-vulkan-trial`): the fence extension was
  correctly absent, but Viz could not allocate its DisplayRead/DisplayWrite/
  Scanout buffer and GPU compositing fell back after crashes. Rejected.
- **X11 GLES**: no suitable EGL configs/DRI3; GPU compositing stayed off.

The GPU process of this Chromium package already ran in the host PID
namespace without seccomp in the baseline; the trial wrappers add no
sandbox-disabling flags. Hardware video decoding is a separate path and works
without browser GPU rendering: see [09-video-decoding.md](09-video-decoding.md).

## Prerequisites

- The tablet already boots the Omarchy desktop on the software profile
  (kernel, vendor modules, `omarchy-desktop.service`, `20-tablet` UWSM
  environment, `hypr` user in `render`/`video` groups): see
  [04-omarchy.md](04-omarchy.md) and the earlier docs.
- Packages on the tablet, as recorded in the build logs: `clang`, `llvm`,
  `meson`, `ninja`, `cmake`, `bison`, `flex`, `libclc`,
  `spirv-llvm-translator`, `python-mako`, `python-ply`, `glslang` (found as
  `glslangValidator`), `vulkan-headers`, `vulkan-tools`, plus the system
  `mesa` (26.2.5 provides Zink), `hyprland` 0.56.2, `aquamarine` 0.15.1.
  Install without replacing the system Mesa graphics libraries.
- About 1 GB of free space for the source and build directory.
- No kernel, boot partition or firmware change is needed. No files have to be
  extracted from the device for the GPU path.

## Reproduction

All commands run on the tablet unless stated. Paths are the ones used on the
original device. Use `tools/tablet` from this repo to reach it.

1. **Get the exact PanVK source** (on any machine with git), then copy it to
   the tablet:

   ```sh
   graphics/panvk/fetch-panvk-source.sh panvk-fourfect
   # copy to the tablet, e.g. /home/hypr/src/panvk-kbase
   ```

   Inputs (pinned): Mesa
   `feeadb5f55f4ba14a5c5ddc84ba20177e419cffb`
   (https://gitlab.freedesktop.org/mesa/mesa), then
   `patches-upstream/0001`…`0005` from
   https://github.com/Noysz/panvk-g99-jm at
   `08aa2e62580e447b28fe419d8995b84d8d6f1fff`, then
   `graphics/panvk/patches/fourfect-kbase-only-build.patch`. Each patch passed
   `git apply --check` on its base; no manual conflict resolution was needed.
   The original tree was assembled by hand from these inputs; for this repo
   it was rebuilt from them and compared byte-for-byte with the tree that was
   compiled on the tablet (identical). The fetch script itself was not run.

   *Our only change* is the build fix: the shared Vulkan library contains CSF
   entry points that reference `panthor_kmod_get_csif_props/get_flush_id`, so
   linking failed when only the `kbase` backend was selected. The patch also
   compiles `panthor_kmod.c` into libpankmod when `kbase` is selected. It adds
   no stubs and does not change device discovery; it does not mean CSF works
   (this GPU is Job Manager).

2. **Build as the desktop user** (outside any PID namespace):

   ```sh
   PANVK_SRC=/home/hypr/src/panvk-kbase bash graphics/panvk/build-panvk-tablet.sh
   ```

   Clang, release, Vulkan-only (`-Dvulkan-drivers=panfrost
   -Dpanfrost-kmds=kbase -Dplatforms=wayland`, no Gallium/EGL/GBM), prefix
   `/opt/omarchy-tablet/gpu/panvk-fourfect`, `nice -n 10 ninja -j4`. The
   original builds ran as transient systemd services
   (`tablet-fourfect-build`, `tablet-fourfect-relink`); the exact
   `systemd-run` line was not recorded. Then as root:

   ```sh
   meson install -C build-tablet --no-rebuild     # in the source directory
   sha256sum /opt/omarchy-tablet/gpu/panvk-fourfect/lib/libvulkan_panfrost.so
   ```

   The working driver's SHA-256 was
   `9662a69671c95a404c6bbe088f2afa362c825f91c2dc3ba72c78c42e1e9f6725`; a
   rebuild will likely differ (toolchain), so treat the hash as a reference.

3. **Install the udev rule** and check it:

   ```sh
   install -m0644 graphics/profile/99-tablet-gpu.rules /etc/udev/rules.d/
   udevadm control --reload && udevadm trigger --subsystem-match=dma_heap
   stat -c '%U:%G %a' /dev/dma_heap/system      # root:render 660
   ```

4. **Run the isolated probes as `hypr`** before touching the desktop. Build the
   tests on the tablet (the exact compile lines were not recorded; these are
   the obvious ones for the includes used):

   ```sh
   d=/opt/omarchy-tablet/diagnostics/gpu; install -d $d
   glslangValidator -V graphics/tests/gpu-smoke.comp -o $d/gpu-smoke.spv
   cc -O2 graphics/tests/gpu-smoke.c -lvulkan -o $d/gpu-smoke
   cc -O2 graphics/tests/egl-smoke.c -lEGL -lGLESv2 -lgbm -ldrm \
      -I/usr/include/libdrm -o $d/egl-smoke
   install -m0755 graphics/panvk/test-panvk-tablet.sh $d/
   export PANVK_TEST_ICD=/opt/omarchy-tablet/gpu/panvk-fourfect/share/vulkan/icd.d/panfrost_icd.aarch64.json
   for t in enumerate compute gles gbm; do $d/test-panvk-tablet.sh $t; done
   dmesg | tail      # look for GPU faults between probes
   ```

   Expected (`gles`):

   ```
   GL renderer: zink Vulkan 1.3(Mali-G57 MC2 (MESA_PANVK))
   GL version: OpenGL ES 3.1 Mesa 26.2.5-arch1.1
   readback: 64 128 191 255; GL error 0
   PASS: hardware GLES shader draw and readback
   PASS: GLES3 transform-feedback capture and readback
   ```

5. **Build and install the shims and profile** (as root). The installed
   shims were built natively on the tablet; that exact command was not
   recorded. The host test harness compiles them like this, which works on
   the tablet too:

   ```sh
   for s in aq-primary-zink aq-software-primary drm-no-cursor; do
     cc -std=c11 -O2 -fPIC -shared -Wall -Wextra -Werror graphics/shim/$s.c -ldl -o /tmp/$s.so
   done
   install -d /opt/omarchy-tablet/gpu
   install -m0755 /tmp/aq-primary-zink.so /opt/omarchy-tablet/gpu/
   install -m0755 /tmp/drm-no-cursor.so /tmp/aq-software-primary.so /opt/omarchy-tablet/   # if not already installed by the desktop setup
   install -m0755 graphics/profile/start-hyprland-gpu /opt/omarchy-tablet/gpu/start-hyprland
   install -m0644 graphics/profile/tablet-gpu-trial-env /opt/omarchy-tablet/gpu/
   install -m0755 graphics/profile/app-env graphics/profile/end-gpu-trial \
                  graphics/profile/prepare-gpu-boot-trial /opt/omarchy-tablet/gpu/
   install -D -o hypr -g hypr -m0644 graphics/profile/omarchy-autostart.lua \
           /home/hypr/.config/default/hypr/autostart.lua
   ```

   Back up first: on the original device the pre-GPU environment, autostart and
   desktop unit were saved under `/opt/omarchy-tablet/backups/pre-gpu`. The
   desktop unit's `ExecStart` must call `/opt/omarchy-tablet/gpu/start-hyprland`
   (as in `omarchy/systemd/omarchy-desktop.service`). With no GPU marker this
   wrapper simply runs the software profile.

6. **Trial in the running session** (volatile; reverts at reboot). Arm the
   rollback first; the notes say a transient timer was used, the exact command
   was not recorded:

   ```sh
   systemd-run --on-active=10min /opt/omarchy-tablet/gpu/end-gpu-trial   # example safety net
   touch /run/tablet-gpu-trial
   systemctl restart omarchy-desktop.service
   ```

7. **One-boot trial** (optional): install `gpu-boot-trial.conf` as a drop-in
   for `omarchy-desktop.service` (e.g.
   `/etc/systemd/system/omarchy-desktop.service.d/gpu-boot-trial.conf`; the
   original drop-in path was not recorded), `systemctl daemon-reload`,
   `touch /opt/omarchy-tablet/gpu/boot-trial`, reboot. The `ExecStartPre`
   consumes the marker and creates `/run/tablet-gpu-trial`, so only that
   boot uses the GPU. Remove the drop-in after the boot passes.

8. **Enable persistently:** `touch /opt/omarchy-tablet/gpu/enabled`, then
   reboot (the verified path) or restart `omarchy-desktop.service`.

### Verify

```sh
pid=$(pgrep -x Hyprland)
grep libvulkan_panfrost /proc/$pid/maps                    # private driver mapped
ls -l /proc/$pid/fd | grep -c mali0                        # GPU in use
for p in $(pgrep -u hypr -x quickshell) $(pgrep -u hypr -x foot); do
  tr '\0' '\n' </proc/$p/environ | grep -E '^(GALLIUM_DRIVER|LIBGL_ALWAYS_SOFTWARE|VK_DRIVER_FILES)='
done                                                       # apps: llvmpipe, 1, no VK_DRIVER_FILES
hyprctl configerrors                                       # empty
systemctl --failed                                         # empty
dmesg | grep -i -E 'mali|kbase.*(fault|error)'             # no new GPU faults
```

On the original device, the final persistent boot showed the custom library
mapped in Hyprland, `GALLIUM_DRIVER=llvmpipe`/`LIBGL_ALWAYS_SOFTWARE=1` in the
shell, Foot and the user manager, empty `configerrors`, no failed units and no
GPU faults.

## How to enable / disable the GPU boot

| Want | Do (as root) |
|---|---|
| GPU now, until reboot | `touch /run/tablet-gpu-trial; systemctl restart omarchy-desktop.service` |
| GPU for the next boot only | drop-in + `touch /opt/omarchy-tablet/gpu/boot-trial`, reboot (step 7) |
| GPU on every boot | `touch /opt/omarchy-tablet/gpu/enabled` |
| Back to software | `/opt/omarchy-tablet/gpu/end-gpu-trial` (removes both markers, restarts the desktop) |

## Pitfalls and lessons

- **Never run stock `omarchy-restart-shell`.** It relaunches Quickshell from
  Hyprland's environment, i.e. with Zink/PanVK variables. Quickshell then maps
  layer surfaces that draw nothing: the bar is invisible but its space stays
  reserved. Restart the shell through `app-env` instead; the repo's
  `shell-plugin/restart-shell` ([10-touch-ui.md](10-touch-ui.md)) does that and checks the new shell has
  `GALLIUM_DRIVER=llvmpipe`. The same applies to anything you start with
  `hyprctl dispatch exec`: wrap it as `/opt/omarchy-tablet/gpu/app-env CMD`.
- **Never let the hardware cursor ioctl through.** Run Hyprland only with
  `drm-no-cursor.so` preloaded and software cursors; one unguarded
  `DRM_IOCTL_MODE_CURSOR` resets the tablet.
- **Do not bypass the fork's OpenGL gate** (`PAN_EXPERIMENTAL_KBASE_GL`) or
  force GLES versions/extensions (`MESA_GLES_VERSION_OVERRIDE` etc.). A
  diagnostic GLES 3.0 override made simple tests pass with the first fork but
  proved nothing about GLES3; FourFect's real transform feedback is what makes
  3.1 valid. `app-env` and `tablet-gpu-trial-env` explicitly unset these
  overrides.
- **Omarchy updates** may change the packaged autostart; re-diff
  `omarchy-autostart.lua` against `/usr/share/omarchy/default/hypr/autostart.lua`.
- **Building PanVK on the tablet is heavy.** USB networking stopped
  responding during compilation while Wi-Fi SSH stayed up and the compositor
  kept running; the original SSH helper took a `TABLET_ADDRESS=<Wi-Fi IP>`
  override for this (check whether `tools/tablet` does). Use `nice` and `-j4`.
- **`PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1`** (PanVK's opt-in for
  non-conformant hardware) and `PANVK_KBASE_USER_CACHE_SYNC=0` were set in
  every validated run; `test-panvk-tablet.sh` and `tablet-gpu-trial-env` set
  both.
- **Window animations are off.** They were reverted on 2026-10-29 while
  chasing Chromium artifacts whose real causes were later found (tiling resizes
  and partly rasterised browser frames, see [09](09-video-decoding.md) and [12](12-performance.md)); they could be
  retried.
- **libmali is a dead end on this firmware.** MediaTek Genio `libmali`
  r32p0 (https://gitlab.com/mediatek/aiot/rity/libmali, `kirkstone`
  `3dff30e37bc305e3d6b00292472110bc0eb93705`, mt8188 and mt8195 builds)
  matches the kernel's UK 11.31, but both builds' compatibility check rejects
  the physical GPU (mt8188 targets 0x90010000 r0p0, mt8195 0x90010000 r0p1;
  the tablet reports 0x9093 r0p1). They also need Arm's `/dev/dma_buf_te` test exporter, which the stock
  kernel lacks (it was built out-of-tree once, see `graphics/historical/`).
  The compatibility check was not bypassed. None of this is needed for the
  final setup. Rockchip libmali builds have no G57/r32 variant.

## Rollback / recovery

- `/opt/omarchy-tablet/gpu/end-gpu-trial` (root) removes
  `/run/tablet-gpu-trial` and `/opt/omarchy-tablet/gpu/enabled` and restarts
  the software desktop. The software shims stay installed.
- If the desktop does not come up at all, the volatile marker is gone after a
  reboot; only `/opt/omarchy-tablet/gpu/enabled` needs removing (over SSH).
- Full revert: restore the files saved in `/opt/omarchy-tablet/backups/pre-gpu`
  (environment, autostart module, desktop unit), remove
  `/home/hypr/.config/default/hypr/autostart.lua` and the udev rule, delete
  `/opt/omarchy-tablet/gpu/panvk-fourfect`.
- No kernel, boot partition, firmware, thermal limit or clock governor was
  changed for the GPU work.

## Upstream references

- PanVK kbase fork, first build: https://github.com/yo-www/panvk
  `efbb4f4697dee645582909aa6fed4802763cb91e` (Mesa 26.3.0-devel), used
  unmodified; superseded by FourFect.
- FourFect/G99 patch set: https://github.com/Noysz/panvk-g99-jm
  `08aa2e62580e447b28fe419d8995b84d8d6f1fff` on Mesa
  `feeadb5f55f4ba14a5c5ddc84ba20177e419cffb`.
- Hyprland 0.56.2 / Aquamarine 0.15.1 / Mesa 26.2.5: Arch Linux ARM packages,
  unmodified. `graphics/historical/patches/hyprland-0.56.2-optional-egl-debug.patch`
  only matters for libmali (Hyprland aborts if `eglDebugMessageControlKHR` is
  missing).
- Chromium 153.0.8010.36 source references for the browser analysis are
  listed in the original notes; key files: `ui/gl/gl_fence_android_native_fence_sync.cc`,
  `gpu/command_buffer/service/shared_image/gl_ozone_image_representation.cc`
  at tag `153.0.8010.36` on https://github.com/chromium/chromium.
