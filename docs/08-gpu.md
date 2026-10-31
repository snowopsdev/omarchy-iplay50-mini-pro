# 08 — GPU: Hyprland compositor on the Mali-G57 (Zink + PanVK/kbase)

The desktop compositor (Hyprland) renders on the GPU through Mesa's Zink
(OpenGL ES on Vulkan) and a privately built PanVK Vulkan driver that talks to
the tablet's own vendor `mali_kbase` kernel driver. Since 2026-10-31 the
Omarchy shell, GTK 4 and Qt applications, mpv and Chromium (page rendering,
compositing, WebGL and the hardware-decoded video) render on the GPU too,
through a second PanVK build with one patch; see
[Applications and Chromium on the GPU](#applications-and-chromium-on-the-gpu-2026-10-31).
Terminals (Foot), GTK 3 and Electron apps still draw on the CPU.
Source files: [`graphics/`](../graphics/README.md).
Work dates: 2026-10-28 (bring-up), 2026-10-29 (follow-ups), 2026-10-31
(applications and browser).

## Status

| Item | Status | Notes |
|---|---|---|
| Hyprland compositing on Mali-G57 MC2 via Zink + PanVK/kbase | **Works** | User physically confirmed Apps, Term, Close and typing are responsive and flicker-free. Passed a one-shot GPU boot and a second normal boot with the persistent profile. No GPU faults logged. |
| Vulkan compute (1024 elements, checked against CPU) | Works | `gpu-smoke`, as the unprivileged desktop user |
| GLES shader draw + readback, transform feedback, GBM linear dma-buf export, mediatek-drm PRIME import, EGL dma-buf import | Works | `egl-smoke` / `egl-smoke gbm`; GLES 3.1 reported, no version overrides |
| Native fence export (`eglDupNativeFenceFDANDROID`) | **Works** (2026-10-31) | `egl-fence-test`: 0 failures in 1000 (500 with the GPU work already finished). Unpatched driver: 200/200 and 10/200 failed |
| Chromium on the GPU (ANGLE GLES-EGL over Zink/PanVK) | **Works**, default since 2026-10-31 | `chrome://gpu`: GPU compositing, GPU rasterisation, WebGL, WebGPU enabled; renderer `ANGLE (Mesa, zink Vulkan 1.3(Mali-G57 MC2 (MESA_PANVK)))`. YouTube, Wikipedia, The Verge and WebGL Aquarium loaded with no GPU-process restart |
| Chromium WebGL Aquarium (500 fish, 1024x1024 canvas) | 41-44 fps | Software profile: 7.4 fps |
| Chromium hardware video + GPU rendering, local 1080p30 clip | 3-6 % frames dropped | Same clip on the software profile: 10-17 %. 4-minute soak (9 x 25 s, windowed and fullscreen alternating): 3.8-6.2 %, no GPU fault, no reset |
| YouTube, 60 fps video, 20 s | GPU: 720p, 1201 frames, 485 dropped (~36 fps shown) | Software: YouTube chose 480p and showed 58 frames in 19 s. YouTube picks a different resolution per browser, so this is not a controlled comparison |
| Chromium page scroll (Wikipedia, 5 s) | 30.6 fps, 95th-percentile frame 51 ms | Software: 28.2 fps, 68 ms. Little gain: scrolling is not bound by rasterisation here |
| Omarchy shell (Quickshell / Qt Quick) on the GPU | **Works**, default since 2026-10-31 | Home screen opened and closed 6 times: 0.17 s of shell CPU time (software: 4.74 s); resident memory 509 MB (software: 674 MB) |
| GTK 4 apps (GL and Vulkan renderers), Qt Quick, mpv (OpenGL), vkcube | **Work** | Correct pixels on screen (screenshots), Mali driver mapped. Checked: Nautilus, mpv, a Qt Quick test window. LibreOffice and Obsidian also open normally under the GPU profile |
| mpv with Vulkan output (`gpu-api=vulkan`, libplacebo) | **Crashes** | Segfault, also with the compositor's unpatched driver and without dma-buf presentation; `~/.config/mpv/mpv.conf` sets `gpu-api=opengl` |
| Electron apps (Obsidian checked), GTK 3 apps, Foot | CPU | They work under the GPU profile but draw in software: Electron's GPU process ends up on llvmpipe, GTK 3 and Foot draw with Cairo/pixman |
| Simple GL window frame rate (glClear + swap, 1280x458) | 28 fps | Through shared dma-heap buffers; 20 fps through the CPU-copy (wl_shm) swapchain. Not investigated further |
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
       autostart: every app command is wrapped in /opt/omarchy-tablet/gpu/app-env
         → gpu-app-env (Zink/kopper on panvk-apps) if gpu/apps-gpu exists, else llvmpipe
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

### Applications and Chromium on the GPU (2026-10-31)

Three separate problems kept applications off the GPU. All three were fixed
without touching the kernel, the system Mesa or the compositor's driver.

1. **Native fences (Chromium).** PanVK on kbase already turned a SYNC_FD
   export of pending GPU work into a real `sync_file`: a kbase soft job
   (`BASE_JD_REQ_SOFT_FENCE_TRIGGER`) that depends on the submission's last
   atom. When the work had already finished, it returned -1 instead. Vulkan
   allows that, but Mesa EGL reports -1 from `eglDupNativeFenceFDANDROID` as a
   failure, and Chromium's GPU process exits on it (the "real pages" failure
   below). `graphics/panvk/patches/panvk-kbase-signalled-export.patch` makes
   that case, and the CPU-wait fallback, return an already-signalled
   `sync_file`: a FENCE_TRIGGER atom with no dependency, which kbase signals
   during the `JOB_SUBMIT` ioctl. It is made once per queue and handed out as
   dups. `PANVK_KBASE_EXPORT_NULL_FD=1` restores the old -1. No kernel module
   was needed (the stock kernel has no `CONFIG_SW_SYNC`).
   This build is installed separately as
   `/opt/omarchy-tablet/gpu/panvk-apps`; the compositor keeps `panvk-fourfect`.
2. **Black OpenGL windows (shell, GTK, Qt, mpv).** There is no DRM render
   node, so Mesa EGL's hardware Wayland path fails and EGL falls back to its
   software path. With `GALLIUM_DRIVER=zink` that path runs Zink as a
   software driver that copies finished frames into `wl_shm` buffers, and
   those arrive black: the frame is correct in the back buffer
   (`glReadPixels`), but the window shows black. This is why the bar went blank
   in the first GPU attempt. The same fallback uses Zink's own Vulkan
   swapchain ("kopper", which presents correctly, like `vkcube`) only when
   both `LIBGL_ALWAYS_SOFTWARE=1` and `MESA_LOADER_DRIVER_OVERRIDE=zink` are
   set (Mesa 26.2.5 `eglapi.c` / `platform_wayland.c`). But Zink also reads
   `LIBGL_ALWAYS_SOFTWARE` and then demands a CPU Vulkan device ("ZINK: CPU
   device requested but none found"). `graphics/shim/zink-kopper-hw.c` hides
   that one variable from code in `libgallium` only, by checking
   `getenv`'s caller. In that library only Zink's device choice and a
   Windows-only d3d12 check read it.
3. **Presenting without a CPU copy.** By default PanVK's window-system code
   on kbase copies every frame into shared memory. `PANVK_KBASE_DRI3=1` (an
   existing fork option) makes swapchain images shared dma-heap buffers that
   Hyprland imports directly: 28 instead of 20 fps in the GL window test,
   with no GPU faults.

**Chromium** keeps the setting it was tested with (`GALLIUM_DRIVER=zink`,
which is correct for its GBM/surfaceless EGL use). Its GPU process gets that
environment from `graphics/browser-gpu/chromium-gpu-process-launcher`,
installed as `/opt/omarchy-tablet/gpu/browser/chromium-gpu-process-launcher`
and named by `--gpu-launcher`. It also loads the hardware video bridge on the
decoder-ready boot, like `video/install/chromium-gpu-launcher`, but without
that launcher's llvmpipe-only `glFinish` workaround. The normal flags change
from `--disable-gpu-rasterization` to `--enable-gpu-rasterization`.

**Everything else** gets its environment from `app-env`, which wraps every
session autostart command, including the environment import that later app
launches inherit. With `/opt/omarchy-tablet/gpu/apps-gpu` present it execs
`gpu-app-env` (the three fixes above, plus the apps ICD); without it, it
keeps the old llvmpipe profile. The autostart module clears the graphics
variables from the systemd user manager before importing, so a variable one
profile does not set cannot linger from UWSM.

### Why apps stayed on llvmpipe (history, superseded)

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

### Chromium on the GPU: what was tried before and why it failed

Superseded on 2026-10-31: the first failure below was the -1 fence export,
fixed in the driver as described above. Kept for the record.

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

9. **Applications and Chromium on the GPU** (2026-10-31). Needs steps 1-8 and,
   for hardware video in the GPU browser, [09-video-decoding.md](09-video-decoding.md).

   a. *Second driver build with the fence patch*, next to the compositor's
      (on any machine, then copy to the tablet; build as `hypr`):

      ```sh
      PANVK_APPS=1 graphics/panvk/fetch-panvk-source.sh panvk-apps
      # on the tablet, source in /home/hypr/src/panvk-apps:
      PANVK_SRC=/home/hypr/src/panvk-apps PANVK_PREFIX=/opt/omarchy-tablet/gpu/panvk-apps \
        bash graphics/panvk/build-panvk-tablet.sh
      meson install -C /home/hypr/src/panvk-apps/build-tablet --no-rebuild   # as root
      ```

      On the project's tablet the source was a copy of the compositor's tree
      with the patch applied (`git apply`), and the build took 11 minutes
      (`-j4`). The installed library's SHA-256 was
      `8c63f59066030de8363371283ea80b9129fb4610fafcc9da9fe0e2262449a0dd`
      (reference only, a rebuild will differ).

   b. *Check the fence fix* (as `hypr`; `egl-fence-test` from `graphics/tests/`):

      ```sh
      cc -O2 graphics/tests/egl-fence-test.c -lEGL -lGLESv2 -o $d/egl-fence-test
      icd=/opt/omarchy-tablet/gpu/panvk-apps/share/vulkan/icd.d/panfrost_icd.aarch64.json
      for m in early late; do
        GALLIUM_DRIVER=zink VK_DRIVER_FILES=$icd PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1 \
          PANVK_KBASE_USER_CACHE_SYNC=0 $d/egl-fence-test $m 500
      done            # both: "fd -1: 0 ... PASS"
      ```

   c. *Install the profile pieces* (root):

      ```sh
      cc -O2 -fPIC -shared -Wall -Wextra -Werror graphics/shim/zink-kopper-hw.c -ldl \
        -o /opt/omarchy-tablet/gpu/zink-kopper-hw.so
      install -m0755 graphics/profile/app-env graphics/profile/gpu-app-env /opt/omarchy-tablet/gpu/
      install -o hypr -g hypr -m0644 graphics/profile/omarchy-autostart.lua \
        /home/hypr/.config/default/hypr/autostart.lua
      install -D -m0755 graphics/browser-gpu/chromium-gpu-process-launcher \
        /opt/omarchy-tablet/gpu/browser/chromium-gpu-process-launcher
      install -D -o hypr -g hypr -m0644 graphics/profile/mpv.conf /home/hypr/.config/mpv/mpv.conf
      ```

      In `/home/hypr/.config/chromium-flags.conf` (back it up first) replace
      `--disable-gpu-rasterization` with `--enable-gpu-rasterization`, and
      `--gpu-launcher=/opt/omarchy-tablet/video/bin/chromium-gpu-launcher`
      with `--gpu-launcher=/opt/omarchy-tablet/gpu/browser/chromium-gpu-process-launcher`.

   d. *Try one app first*, e.g. as `hypr` in the session:
      `/opt/omarchy-tablet/gpu/gpu-app-env nautilus`, and the shell with
      `shell-plugin/restart-shell gpu` (expect `shell restarted (Mali GPU)`).
      `graphics/tests/egl-wayland-test.c` (build line in the file) is the
      minimal check: under `gpu-app-env` it must report `zink ... MESA_PANVK`
      and the window must cycle red/green/blue, not stay black.

   e. *Make it the default:* `touch /opt/omarchy-tablet/gpu/apps-gpu`, then
      restart `omarchy-desktop.service` (or log in again). Restart Chromium
      (`systemctl --user restart tablet-chromium-warm.service` as `hypr`, or
      close it) to pick up the new flags.

### Verify

```sh
pid=$(pgrep -x Hyprland)
grep libvulkan_panfrost /proc/$pid/maps                    # private driver mapped
ls -l /proc/$pid/fd | grep -c mali0                        # GPU in use
q=$(pgrep -u hypr -x quickshell | head -1)
grep -c panvk-apps/lib/libvulkan_panfrost.so /proc/$q/maps # >0: shell on the GPU (apps-gpu set)
for p in $(pgrep -f type=gpu-process); do                 # Chromium's GPU process
  grep -c panvk-apps /proc/$p/maps                         # >0 (the 0 line is pgrep's own shell)
done
runuser -u hypr -- env XDG_RUNTIME_DIR=/run/user/1001 systemctl --user show-environment \
  | grep -E 'MESA_LOADER|VK_DRIVER_FILES|DRI3'             # what later app launches inherit
hyprctl configerrors                                       # empty
systemctl --failed                                         # empty
dmesg | grep -i -E 'mali|kbase.*(fault|error)'             # no new GPU faults
```

On the original device, the final persistent boot showed the custom library
mapped in Hyprland, `GALLIUM_DRIVER=llvmpipe`/`LIBGL_ALWAYS_SOFTWARE=1` in the
shell, Foot and the user manager, empty `configerrors`, no failed units and no
GPU faults.
After step 9 (desktop restarted with `apps-gpu`), the shell and Chromium's
GPU process mapped `panvk-apps`, Chromium's GPU process also mapped the video
bridge, and the user manager held `MESA_LOADER_DRIVER_OVERRIDE=zink`, the
apps ICD and `PANVK_KBASE_DRI3=1`.

## How to enable / disable the GPU boot

| Want | Do (as root) |
|---|---|
| GPU now, until reboot | `touch /run/tablet-gpu-trial; systemctl restart omarchy-desktop.service` |
| GPU for the next boot only | drop-in + `touch /opt/omarchy-tablet/gpu/boot-trial`, reboot (step 7) |
| GPU on every boot | `touch /opt/omarchy-tablet/gpu/enabled` |
| Back to software | `/opt/omarchy-tablet/gpu/end-gpu-trial` (removes both markers, restarts the desktop) |
| Apps and shell on the GPU | `touch /opt/omarchy-tablet/gpu/apps-gpu`, log in again (restart `omarchy-desktop.service`) |
| Apps and shell back on llvmpipe | `rm /opt/omarchy-tablet/gpu/apps-gpu`, log in again; `shell-plugin/restart-shell software` switches only the shell, until the next login |
| Chromium back on llvmpipe | `touch /opt/omarchy-tablet/gpu/browser/software`, restart Chromium (the GPU-process launcher then hands over to the old one; GPU rasterisation stays requested but runs on llvmpipe) |

## Pitfalls and lessons

- **Never run stock `omarchy-restart-shell`.** It relaunches Quickshell from
  Hyprland's environment (`GALLIUM_DRIVER=zink`), so its windows come out
  black (fix 2 above): the bar is invisible but its space stays reserved.
  Restart the shell through `app-env` instead; the repo's
  `shell-plugin/restart-shell` ([10-touch-ui.md](10-touch-ui.md)) does that and reports
  which renderer the new shell got. The same applies to anything you start with
  `hyprctl dispatch exec`: wrap it as `/opt/omarchy-tablet/gpu/app-env CMD`.
- **`GALLIUM_DRIVER=zink` is wrong for Wayland windows, right for Chromium.**
  Windows need `gpu-app-env` (kopper); Chromium's GPU process needs
  `GALLIUM_DRIVER=zink` (its launcher sets it). An app that renders correctly
  into its back buffer but shows black is on the wrong one.
- **mpv's Vulkan output crashes on PanVK**; keep `gpu-api=opengl`.
- **Not tested on the GPU profile:** long sessions of the shell on the GPU
  (only restarts and short checks), suspend/resume, battery impact, Kdenlive
  and OBS. The two unexplained resets from the video work
  ([09](09-video-decoding.md)) did not recur in the 4-minute soak, which
  proves little either way.
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
- Applications/browser (2026-10-31): `rm /opt/omarchy-tablet/gpu/apps-gpu`
  and log in again; restore `chromium-flags.conf` from its backup (the
  project's is `/opt/omarchy-tablet/backups/chromium-flags.conf.pre-gpu-*`) or
  touch `/opt/omarchy-tablet/gpu/browser/software`. The previous `app-env`,
  `gpu-app-env` and autostart module are in
  `/opt/omarchy-tablet/backups/apps-gpu-*`. Remove `~hypr/.config/mpv/mpv.conf`
  if mpv should pick its own API again.
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
