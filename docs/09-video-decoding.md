# 09 — Hardware video decoding (H264) in Chromium

Chromium decodes H264 on the MT6789 video decoder (VDEC) through the tablet's
own MediaTek vendor stack: the stock kernel modules, the stock 32-bit Android
`v3avpud` VCU daemon (run natively in a private chroot), and a small V4L2
compatibility bridge loaded only into Chromium's GPU process. Since
2026-10-31 the page and the decoded video are rendered on the Mali GPU
([08-gpu.md](08-gpu.md#applications-and-chromium-on-the-gpu-2026-10-31)): the
GPU process is started by `gpu/browser/chromium-gpu-process-launcher`, which
loads the same bridge. The software-rendered setup described below (with
`video/bin/chromium-gpu-launcher`) remains as the fallback.
Sources: [`video/`](../video/README.md). Work dates: 2026-10-28 to 2026-10-29.

## Status

| Item | Status | Evidence / notes |
|---|---|---|
| Native H264 decode, Baseline 320x240 | **Works** | 12/12 frames, first frame identical to an FFmpeg software decode |
| Native H264 decode, High 1280x720 with B-frames | **Works** | 60/60 frames, exact first-frame match |
| Native H264 decode, High 1920x1080 with B-frames | **Works** | 30/30 frames, exact first-frame match |
| Chromium 153 uses the hardware decoder | **Works** | `V4L2VideoDecoder`, `kIsPlatformVideoDecoder=true`, normal profile and real YouTube |
| Survives reboot | **Works** | Controlled reboot 2026-10-28 22:01: service ready, guarded access granted, YouTube decoded in hardware again |
| 10-minute YouTube 1080p fullscreen | Works, drops frames | 600.8 s, 18022 frames / 1652 dropped (~9.2 %), USB debug gadget unbound; no stall or media error |
| Post-reboot YouTube 1080p | Works, drops frames | 150 s uninterrupted main content: 4505 frames / 755 dropped (~16.8 %) |
| Browser presentation | GPU since 2026-10-31 | Local 1080p30 clip: 3-6 % frames dropped on the GPU vs 10-17 % on llvmpipe; 4-minute soak 3.8-6.2 %, no reset (see [08-gpu.md](08-gpu.md)). The YouTube numbers above were taken on llvmpipe |
| YouTube 1080p30 on the GPU, NV12 view (2026-11-02) | **Works, ~2 % dropped** | 90 s (45 s windowed + 45 s fullscreen): 22/1349 and 25/1350 dropped. The GPU launcher's earlier BGR4 view dropped ~23 % (104/450, 98/449), which the user saw as stutter. Same frame captured in both modes: PSNR 49 dB |
| YouTube 1080p60 on the GPU, NV12 view | Drops ~14 % (open) | 15 s each: 122/899 windowed, 132/900 fullscreen; 720p60 drops as much. Not decode-bound (the decoder returns 60.5 frames/s) and not GPU-bound (fragment slot ~43 % busy): Chromium's Wayland present loop is. See Pitfalls |
| VP8 / VP9 / AV1 / HEVC | **Not validated** | The driver advertises more codecs; only H264 was ever decoded. The YouTube extension hides VP8/VP9/AV1 |
| Protected (secure) playback | Not supported | Secure decode paths are never used |
| HDR, 10-bit, 1080p60, 4K | Unverified | HDR/unknown colour metadata falls back to NV12; nothing beyond 1080p30 was measured |
| Frame drops with `TABLET_CHROMIUM_GL_FINISH_ALWAYS=1` | Not measured | The artifact fix was checked for correctness only |
| Two unexplained resets during sustained trials (2026-10-28 21:24, 21:36) | **Open** | Cause unknown; see Pitfalls |

## How it works

```
Chromium (normal profile, flags in ~/.config/chromium-flags.conf)
  --gpu-launcher=/opt/omarchy-tablet/video/bin/chromium-gpu-launcher
     └─ GPU process (exec'd, not forked: --no-unsandboxed-zygote)
          LD_PRELOAD chromium-gl-finish-loader.so : mt6789-v4l2-bridge.so : drm-no-cursor.so : aq-software-primary.so
          Chromium V4L2StatefulVideoDecoder ──MMAP API──► bridge ──DMABUF──► /dev/video0
                                                           │                (mtk-vcodec-dec, guarded copy)
                                                           └─ M21S → NV12/BGR4 detile (libyuv) into real DMA buffers
                                                              from /dev/dma_heap/mtk_mm
/dev/video0 ◄── IPI ── mtk-vcu ◄──/dev/vcu── v3avpud (stock ARM32 Bionic daemon)
                                              in chroot + new PID/mount namespace,
                                              read-only Android system + runtime APEX
omarchy-tablet-vcu.service (Type=notify) supervises the daemon, loads the guarded
decoder module, and grants root:video access only after live checks.
```

1. **Kernel side.** Stock vendor modules from `/vendor_dlkm/lib/modules`,
   loaded in a fixed order by `prepare-trial.sh`: `mtk-vcodec-common`, `vcp`,
   the GenieZone/Trusty stack (`gz_trusty_mod`, `gz_irq_mod`, **`gz_ipc_mod`
   before `gz_virtio_mod`**, `gz_tz_system`, `iommu_gz`, `trusted_mem`,
   `mtk_sec_heap`, `gz_main_mod`), `cmdq-sec-drv`, `mtk-vcu` (creates
   `/dev/vcu`). The decoder itself (`mtk-vcodec-dec-v1`) is a private copy of
   the stock module with two instructions changed (below), loaded by the
   supervisor only after the daemon is ready.
2. **VCU daemon.** Stock Android starts `/vendor/bin/v3avpud -f` (32-bit ARM,
   Bionic, API 31). The decoder module forwards work to it over IPI; loading
   the module without the daemon cannot decode. `supervisor.py` runs it with
   `unshare --mount --pid --mount-proc --fork --kill-child=KILL`;
   `runtime-inner.sh` builds a chroot under `/run/omarchy-tablet-vcu/root` with
   read-only binds of the Android `system` tree, `/vendor`, the extracted
   runtime APEX (Bionic linker/libc) and a private `ld.config.txt`, plus only
   `/dev/{vcu,null,zero,urandom,random}`, the device-tree and one THP sysfs
   file. No Android property service, binder or HALs are started.
   Readiness (`READY=1`) requires: both `receiver` threads blocked in
   `VCU_GET_OBJECT` (0xc058760a), the main thread waiting in the futex join
   that follows the initial `VCU_SET_OBJECT` ACK, the guarded decoder loaded
   with exactly one node on platform `16000000.vdec` set to root:root 0600,
   and a successful `v4l2-query` (MediaTek caps, H264 input, M21S capture).
3. **Access gate.** `71-omarchy-tablet-vcu-root-only.rules` keeps the decoder
   root-only. `browser-access.py --grant` (ExecStartPost) opens the decoder
   and the `mtk_mm` heap to root:video 0660 only if the root-owned opt-in
   `/opt/omarchy-tablet/video/browser-enabled` exists, the supervisor is the
   unit's MainPID, the module file hash matches, and `check-memory-guard`
   proves on the live module that MMAP is refused (EINVAL) and DMABUF accepted
   (caps 0x14) on both queues. It then writes
   `/run/omarchy-tablet-video-ready` (boot_id, supervisor PID, tested caps).
   Stop/failure paths remove the marker and return both nodes to root 0600.
4. **V4L2 bridge** (`hw-driver/compat/mt6789-v4l2-bridge.c`). Activates only
   with `TABLET_V4L2_BRIDGE=1`, in the exec'd `--type=gpu-process`
   (`TABLET_V4L2_BRIDGE_PROCESS=gpu`), on a node whose QUERYCAP reports driver
   `mtk-vcodec-dec`, card `platform:mt6789`. Chromium sees ordinary MMAP
   queues; the kernel only sees DMABUF from `mtk_mm`. Bitstream is copied into
   a real DMA buffer on QBUF. Capture stays native M21S (contiguous MM21 tiles);
   on DQBUF it is detiled with libyuv into a separate real NV12 buffer, or
   into opaque packed BGRA (`TABLET_V4L2_VIEW_FORMAT=BGR4`) when the firmware
   reports SDR BT.709/BT.601 colour metadata (otherwise NV12 is kept). Only the
   llvmpipe fallback launcher uses BGR4; the GPU launcher uses NV12 and lets the
   GPU convert (see Pitfalls). It
   supplies the vendor's `request_fd = -1` "no supplemental buffer" sentinel,
   keeps separate native and client buffer pools (`DECOUPLE_CAPTURE`,
   `CAPTURE_HEADROOM=8`) so retained frames are never overwritten, bounds
   `poll` (`POLL_BACKPRESSURE`), and defers re-queueing the LAST buffer after
   `START` until new input is accepted. It refuses secure decode, unknown
   formats, unsafe geometry and >32 buffers, and changes no sandbox policy.
5. **GL finish loader** (`hw-format/chromium-gl-finish-loader.c`). Chromium's
   GPU process renders with ANGLE GLES on Mesa llvmpipe. For any explicit
   EGL provider it replaces `glFlush` with that provider's real `glFinish`
   when a NV12 decode context exists or when
   `TABLET_CHROMIUM_GL_FINISH_ALWAYS=1`; otherwise it passes `glFlush`
   through. Its `dlsym` hook is a small assembly trampoline so `RTLD_NEXT` /
   `RTLD_DEFAULT` keep the original caller's scope.
6. **Launcher** (`install/chromium-gpu-launcher`). Before adding the two
   libraries it checks the ready marker: root-owned, same boot_id, service
   active/running with the recorded MainPID, guard caps as tested, read/write
   access to both nodes. Otherwise it starts the GPU process with only the
   desktop preloads, and Chromium silently uses its software decoder.
7. **YouTube.** `youtube-h264/` is a Manifest V3 content script on
   `*.youtube.com` that makes `MediaSource.isTypeSupported` /
   `canPlayType` reject `av01`, `vp9`/`vp09`, `vp8`, so YouTube serves H264.
   No other sites are affected and it has no extra permissions. YouTube's
   **Ambient mode** must be off (player settings); its two blurred full-screen
   canvases were the largest single rendering cost (e.g. 1080p30 H264,
   ~10 s: 215 dropped with Ambient on vs 44 off).

### Chromium artifact fixes (2026-10-29)

The user saw artifacts while Chromium loaded. A `kmsgrab` scanout capture
found two causes:

1. **Tiling resizes.** Side-by-side tiling resized the visible Chromium
   window, which then showed cropped, striped stale frames for ~0.5 s
   (every resize is a full software re-render). Fix: the `monocle` layout in
   the user's `~/.config/hypr/looknfeel.lua` (one app at a time; floating
   windows unaffected): `hl.config({ general = { layout = "monocle" } })`.
   The full final file is
   [`omarchy/staged/looknfeel.lua`](../omarchy/staged/looknfeel.lua). See
   [12-performance.md](12-performance.md).
2. **Partly rasterised frames.** With no NV12 context, the loader passed
   Chromium's `glFlush` through; llvmpipe's `glFlush` does not wait for
   rasterisation, so frames with unrasterised 64x64 tiles (staircase holes
   showing the background) reached the compositor. Fix:
   `TABLET_CHROMIUM_GL_FINISH_ALWAYS=1`, read at library load (it survives
   Chromium's environment sanitising), always forces the real `glFinish`. The
   launcher in this repo already sets it. Same new-window launch before/after:
   frames with holes 32/32 → 0/60. Host policy tests: 14/14. Scrolling
   (~30 fps) and time to first frame were unchanged ([12-performance.md](12-performance.md)).

## Prerequisites

- Doc 08's desktop with `/opt/omarchy-tablet/drm-no-cursor.so` and
  `aq-software-primary.so` installed (the launcher keeps them in the preload).
- Hardware bring-up service `omarchy-hw.service` (hardware bring-up, [docs/03](03-boot-and-rootfs.md)): it
  mounts the stock `/vendor` read-only and provides `/vendor_dlkm/lib/modules`.
  The VCU unit `Requires=` it. The kernel must have PID namespaces (enabled in
  this project's kernel config).
- Packages: Chromium **153.0.8010.36-1** from Arch Linux ARM (its PKGBUILD
  builds `use_v4l2_codec=true`, `use_vaapi=false`; recipe pinned at
  https://github.com/archlinuxarm/PKGBUILDs/blob/3d45100a45e945ac14f0fbb0c6e67c5540f9e6c9/extra/chromium/PKGBUILD),
  `libyuv` (headers + library), `gcc`, `python`, `util-linux`
  (`unshare`), `device-mapper` (`dmsetup`), `systemd`. On a desktop for
  fixtures and checks: `ffmpeg`, `python`, `libyuv`, `llvm` (for
  `elf-closure.py`).
- **From your own tablet** (never distributed here):
  - Stock decoder module `/vendor_dlkm/lib/modules/mtk-vcodec-dec-v1.ko`.
    `guard-mmap-module.py` only accepts SHA-256
    `0a5d904c798a35297e4afd9af76f21556f10544bcee0fb4c9f416429723227b7`;
    a different firmware build needs its own analysis, do not force it.
  - The other modules listed above and `/vendor/bin/v3avpud` with its
    `/vendor/lib` libraries (read in place from the mounted vendor partition).
  - The Android `system` logical partition inside `super` (`/dev/sdc60` on
    this tablet), exposed read-only through device-mapper. You must write
    `/opt/omarchy-tablet/video/system-ro.table`: a `dmsetup` *linear* table
    (512-byte sectors) listing that partition's extents in `super`. The
    original table had 7 extents; how it was generated is **not recorded**
    (the logical-partition metadata can be read with `lpdump` from
    `android-tools`). `prepare-trial.sh` also hard-codes the `super` size
    (`9663676416` bytes) as a safety check; adjust it only after confirming
    your device node and size.
  - The runtime APEX payload: `apex_payload.img` (an ext4 image) from the
    `com.android.runtime` APEX zip, saved as
    `/opt/omarchy-tablet/video/runtime-apex/apex_payload.img`. The exact
    extraction command and source path were not recorded. On the original
    device the retained system Bionic was API 36 while the vendor daemon is
    API 31; this combination passed the linker preflight.

## Reproduction (root on the tablet unless stated)

1. **Inventory (read-only).** `sh video/tools/decoder-inventory.sh`. Before
   setup it showed no `/dev/video*`, `/dev/vcu*`, and only `vcp_status` plus
   the video clock module loaded.
2. **Layout and Android pieces.**

   ```sh
   b=/opt/omarchy-tablet/video
   install -d $b/runtime $b/probes $b/modules $b/lib $b/bin $b/android-system \
              $b/runtime-apex/mnt $b/vcu-root/system $b/vcu-root/vendor \
              $b/vcu-root/apex/com.android.runtime
   # write $b/system-ro.table and $b/runtime-apex/apex_payload.img (see Prerequisites)
   install -m0755 video/prepare-trial.sh $b/
   $b/prepare-trial.sh     # expect LOAD lines, /dev/vcu, and a driver link for trusty:gz-main
   ```

   `prepare-trial.sh` mounts everything read-only (`ro,noload`), refuses a
   writable mapping, and loads only modules that are not yet loaded. It does
   not load the decoder.
3. **Native ARM32 execution.** If `qemu-user` binfmt is registered, it
   intercepts the 32-bit daemon (it duplicated loader arguments). Mask only
   `qemu-arm`, keeping every other rule; the script backs up what it changes
   under `/opt/omarchy-tablet/backups/native-arm-<UTC>/`:

   ```sh
   install -m0755 video/hw-userspace/service/native-arm-policy.py $b/runtime/
   $b/runtime/native-arm-policy.py --install && $b/runtime/native-arm-policy.py --check
   ```

   Optional linker preflight (does not run the daemon's code):
   `video/hw-userspace/probe-runtime.sh ROOT`.
4. **Guarded decoder module.** Stock `mtk-vcodec-dec-v1` advertises MMAP but
   its worker dereferences the capture buffer's `dma_buf` unconditionally, so
   an ordinary MMAP client crashed the kernel (NULL+8 fault, automatic
   reboot). The script changes `.text+0x10218` and `+0x10434` from
   `movk x8,#0x11,lsl#32` to `movk x8,#0x10,lsl#32` (both queues keep
   `VB2_DMABUF`, drop `VB2_MMAP`); ABI, vermagic and relocations stay
   identical.

   ```sh
   python3 video/hw-driver/compat/guard-mmap-module.py stage \
       /vendor_dlkm/lib/modules/mtk-vcodec-dec-v1.ko $b/modules/mtk-vcodec-dec-v1-dmabuf-only.ko
   python3 video/hw-driver/compat/guard-mmap-module.py verify \
       /vendor_dlkm/lib/modules/mtk-vcodec-dec-v1.ko $b/modules/mtk-vcodec-dec-v1-dmabuf-only.ko
   sha256sum $b/modules/mtk-vcodec-dec-v1-dmabuf-only.ko
   # expected 0a9643e1ec91ea1631841f14ae5b223fa7be00d745b9e24df3740d925f76ffca (supervisor and gate check this)
   ```

5. **Probes** (build on the tablet; commands from the notes):

   ```sh
   cc -O2 -Wall -Wextra -Werror video/hw-driver/v4l2-query.c -o $b/probes/v4l2-query
   cc -O2 -Wall -Wextra -Werror video/hw-driver/compat/check-memory-guard.c -o $b/probes/check-memory-guard
   cc -O2 -Wall -Wextra -Werror video/hw-driver/v4l2-decode.c -o /root/v4l2-decode
   ```

6. **Service, root-only first.**

   ```sh
   install -m0755 video/hw-userspace/service/{supervisor.py,runtime-inner.sh,browser-access.py} $b/runtime/
   install -m0644 video/hw-userspace/ld.config.txt $b/runtime/
   install -m0644 video/hw-userspace/service/71-omarchy-tablet-vcu-root-only.rules /etc/udev/rules.d/
   udevadm control --reload          # before any decoder node appears
   install -m0644 video/hw-userspace/service/omarchy-tablet-vcu.service /etc/systemd/system/
   systemd-analyze verify /etc/systemd/system/omarchy-tablet-vcu.service
   systemctl daemon-reload && systemctl start omarchy-tablet-vcu.service
   systemctl show omarchy-tablet-vcu.service -p ActiveState -p SubState -p StatusText
   stat -c '%U:%G %a %n' /dev/video0 /dev/dma_heap/mtk_mm   # root:root 600 both
   test ! -e /run/omarchy-tablet-video-ready
   ```

   Expected status text: `Native VCU ready; browser access controlled by
   explicit opt-in`.
7. **Prove real decoding before any browser use.** Generate the fixture on a
   desktop (`python3 video/hw-driver/make-sample.py` → `sample/`, needs
   ffmpeg with libx264), copy `sample/` to the tablet, then as root:

   ```sh
   /root/v4l2-decode --mtk --capture=native /dev/video0 sample/testsrc-320x240.au /tmp/NEW1
   python3 video/hw-format/check-m21s.py /tmp/NEW1.plane0.bin --candidate-contiguous-mm21 \
       --coded-width 320 --coded-height 256 --stride 320 --reference sample/first-frame.yuv420p
   ```

   Require 12 returned frames, LAST, clean STREAMOFF/close, a pixel match, and
   no new kernel messages. Use the geometry the probe actually reports. The
   original run reported M21S, coded 320x256, one plane of 122880 bytes,
   stride 320.
8. **Enable at boot**, reboot once, and repeat the checks of step 6:
   `systemctl enable omarchy-tablet-vcu.service`.
9. **Bridge, loader, launcher, extension.** Build on the tablet (commands
   from the notes, output paths as installed):

   ```sh
   cd video/hw-driver/compat
   cc -O2 -fPIC -shared -Wall -Wextra -Werror mt6789-v4l2-bridge.c \
      ../../hw-format/m21s-convert.c ../../hw-format/m21s-argb.c \
      -ldl -lpthread -lyuv -o $b/lib/mt6789-v4l2-bridge.so
   cd ../../hw-format
   cc -std=c11 -O2 -fPIC -shared -Wall -Wextra -Werror chromium-gl-finish-loader.c \
      -ldl -pthread -o $b/lib/chromium-gl-finish-loader.so
   cd ../..
   install -m0755 video/install/chromium-gpu-launcher $b/bin/
   install -d $b/youtube-h264 && install -m0644 video/youtube-h264/* $b/youtube-h264/
   ```

   The copy/install step was done interactively on the original device; the
   paths above are the installed paths recorded in `install/final-health.py`.
   Optional bridge-level native check (exact command from the notes):

   ```sh
   cc -O2 -Wall -Wextra -Werror video/hw-driver/compat/mmap-decode.c -ldl -o /tmp/tablet-mmap-decode
   TABLET_V4L2_BRIDGE=1 LD_PRELOAD=$b/lib/mt6789-v4l2-bridge.so \
     /tmp/tablet-mmap-decode --capture=NV12 /dev/video0 sample/testsrc-320x240.au /tmp/NEW2
   python3 video/hw-driver/check-frame.py /tmp/NEW2 --format NV12 --coded-width 320 \
     --coded-height 256 --stride-y 320 --stride-uv 320 --reference sample/first-frame.yuv420p
   ```

10. **Opt in to browser access.**

    ```sh
    install -m0600 /dev/null $b/browser-enabled
    systemctl restart omarchy-tablet-vcu.service
    cat /run/omarchy-tablet-video-ready; cat /proc/sys/kernel/random/boot_id   # same boot_id
    stat -c '%U:%G %a %n' /dev/video0 /dev/dma_heap/mtk_mm                    # root:video 660
    ```

    The desktop user (and therefore Chromium's GPU process) needs `video`
    group access to both nodes; check with `id hypr`.
11. **Chromium flags.** `python3 video/install/enable-browser.py` (root) backs
    up `/home/hypr/.config/chromium-flags.conf` to
    `/opt/omarchy-tablet/backups/chromium-video-<UTC>/` and merges in:

    ```
    --enable-features=…,AcceleratedVideoDecoder
    --load-extension=…,/opt/omarchy-tablet/video/youtube-h264
    --disable-features=Vulkan,SkiaGraphite
    --use-gl=angle
    --use-angle=gles-egl
    --ignore-gpu-blocklist
    --disable-gpu-rasterization
    --no-unsandboxed-zygote
    --gpu-launcher=/opt/omarchy-tablet/video/bin/chromium-gpu-launcher
    ```

    Existing Omarchy extensions, password store and touch features are kept.
    No remote-debugging or sandbox-disabling flag is stored.
12. **Restart Chromium**, turn YouTube Ambient mode off once in the player
    settings, and set the Hyprland `monocle` layout (above).
13. **USB power.** During the sustained tests the board's 1.2 A SDP charger
    setting exceeded what the attached USB port advertised (500 mA) and USB/VBUS
    cycled. The fix (`omarchy-usb-host-current.service`) belongs to the power
    work: see
    [11-power-battery.md](11-power-battery.md) and `power/tools/USB-HOST-CURRENT.md`.

### Verify

- `chrome://media-internals` for a playing video: decoder `V4L2VideoDecoder`,
  `kIsPlatformVideoDecoder: true`. A green `chrome://gpu` label alone proves
  nothing.
- `python3 video/install/final-health.py` (root, read-only) prints services,
  the ready marker, installed hashes and whether each Chromium process maps
  the bridge (only the GPU process should).
- On YouTube, "Stats for nerds" should show `avc1`.
- `video/install/monitor-playback.py [seconds]` samples VCU/watchdog thread
  scheduling during playback; `video/tools/*.mjs|js` were used over the
  Chrome DevTools protocol for frame/drop measurements (they need a
  temporary `--remote-debugging-port`; remove it afterwards).

## Pitfalls and lessons

- **60 fps video is limited by Chromium's Wayland present loop (2026-11-02).**
  Every 60 fps video drops ~10-15 % of frames, at 720p as at 1080p, so it is
  not pixel throughput. Measured during 1080p60: V4L2 tracepoints show the
  decoder returning 60.5 frames/s with buffers back in 2-5 ms; the Mali fragment
  slot is ~43 % busy; Hyprland flips whenever a new buffer is there. Chromium
  only commits ~52 frames/s: a Chromium trace shows each frame taking ~48 ms
  (median) from draw to presentation feedback, with at most 2 swaps pending
  ("Swap throttled"), because the GPU process waits on the CPU for its GPU fence
  (~8 ms) before the browser may commit (no explicit sync: the MediaTek DRM driver
  has no syncobj support, `DRM_CAP_SYNCOBJ` = 0), then for Hyprland's frame
  callback. Tried without effect: a PanVK import cache that removes Chromium's
  per-frame re-import of the video dma-buf (98 % hits, ~6 % less GPU-process CPU;
  `graphics/panvk/patches/experimental/`), `--enable-hardware-overlays` (the
  video was never promoted to a subsurface), `--disable-gpu-vsync`, pinning to
  big cores. Real fixes would need explicit sync (kernel syncobj support plus real
  GPU fences from PanVK) or more pending swaps; neither is a setting.
 It reports NEON but runs the C
  rows: `NV12ToARGB` at 1920x1088 takes 11.2 ms on a Cortex-A76 and 33.7 ms on
  a Cortex-A55, the same with NEON masked off. The BGR4 view (detile + NV12 to
  ARGB per frame) therefore missed the 33 ms frame budget whenever the thread
  ran on a little core: ~23 % of 1080p30 frames dropped on the GPU browser, and
  `perf` showed 60 % of the GPU process in `NV12ToARGBRow_C`. On the GPU use the
  NV12 view (detile only, ~0.9 ms); BGR4 only made sense for llvmpipe.

- **The daemon must be ready before anything opens the decoder.** The stock
  module's open path queries the daemon (`vdec_if_get_param`) even for format
  enumeration. The udev rule and the gate exist so nothing (including
  Chromium's probing) opens `/dev/video0` early.
- **Never let an unadapted MMAP client at the stock module.** It crashed the
  kernel through a NULL `dma_buf` in the decode worker. Keep the guarded module
  and the root-only default; permissions do not revoke already-open fds, so
  close trial browsers before withdrawing access.
- **`request_fd` = 0 means fd 0 to this driver.** The vendor QBUF reads
  `v4l2_buffer` offset 0x50 as an optional supplemental DMA-buf fd; "none" is
  `-1`. The bridge (and `v4l2-decode --mtk`) supply it.
- **Bionic's 16-bit thread-ID limit.** The 32-bit daemon aborted before
  opening `/dev/vcu` because host TIDs exceeded 65535 (Bionic mutex owner
  field). A fresh PID namespace per start keeps IDs small; do not lower the
  global `pid_max`. The VCU/decoder modules store task pointers and raw TGIDs,
  not namespace-relative PIDs, so the namespaced daemon works.
- **`qemu-arm` binfmt hijacks native ARM32 exec** even inside a chroot. Mask
  only that entry (`native-arm-policy.py`).
- **Module order matters functionally, not just for linking:** `gz_ipc_mod`
  must be loaded before `gz_virtio_mod` or TIPC never goes online.
  `cmdq-sec-drv` keeps initialising for ~10 s after insmod. Do not unbind or
  unload the TIPC virtio transport: its remove path clears a pointer it later
  uses; reboot instead. The protected CMDQ session still fails, which does not
  affect non-secure H264.
- **Firmware chose M21S, not NV12.** `S_FMT NV12` returned success but `G_FMT`
  reverted to M21S. M21S here is uncompressed 8-bit contiguous MM21
  (16x32 Y / 16x16 UV tiles, UV after `stride*coded_height`), validated by
  exact pixel comparison at all three geometries. Never relabel it as NV12;
  the bridge really converts it.
- **Chromium's sandboxed GPU process is forked from the zygote**, so
  `LD_PRELOAD` constructors never run as the GPU process. The flags
  `--no-unsandboxed-zygote` + `--gpu-launcher` make it an exec'd process. On
  this ARM package the GPU process already ran in the host PID namespace with
  seccomp off in the baseline; renderers keep their PID/network namespace and
  seccomp. No sandbox flag was disabled.
- **Chromium passes ioctl numbers as `int`**, so high-bit V4L2 requests arrive
  sign-extended; the bridge normalises to 32 bits as the kernel does.
- **A short local clip is not a test.** Several candidates passed a simple
  clip and failed real YouTube (fences, loop teardown). Validate with the
  real site, fullscreen, ad-guarded, using actual media-time progress and
  `getVideoPlaybackQuality()`.
- **Codec choice alone did not fix fullscreen smoothness**: AV1 and H264
  dropped similar numbers at native density before Ambient mode was turned
  off. The remaining cost is CPU rendering at 1920x1200.
- **Unexplained resets.** Two sustained trials ended in resets (21:24, 21:36
  on 2026-10-28). The boot reason "Watchdog" also appears after a normal
  `systemctl reboot` on this port, so it proves nothing; the old pstore console
  is stale Android output. The vendor active-buffer warnings at stream
  teardown were removed by the deferred-LAST fix, and the USB current cap
  stopped USB cycling, but neither is proven to be the reset cause. Resets
  without video also occurred earlier in the project (audio, bulk module
  loading). No watchdog, RT-priority, CPU-frequency or thermal settings were
  changed.
- The Android partitions are only ever mounted read-only; no module is
  force-loaded and no secure-memory path is stubbed.

## Rollback / recovery

- **Browser only** (keeps the root-only VCU service): close Chromium, then

  ```sh
  rm -f /opt/omarchy-tablet/video/browser-enabled
  /opt/omarchy-tablet/video/runtime/browser-access.py --lock
  stat -c '%U:%G %a %n' /dev/video0 /dev/dma_heap/mtk_mm   # root:root 600
  ```

  and restore the backed-up `chromium-flags.conf` (owner hypr:hypr). Without
  the ready marker the launcher already falls back to software decoding.
- **Always-finish only:** remove `TABLET_CHROMIUM_GL_FINISH_ALWAYS=1` from
  `chromium-gpu-launcher` (restores flush-through when no NV12 context).
- **Whole stack:** `systemctl disable --now omarchy-tablet-vcu.service`,
  remove the unit and the udev rule; restore the binfmt files listed in the
  `native-arm-<UTC>` backup manifest and reload binfmt rules. Loaded vendor
  modules stay until reboot (no unload is attempted); a reboot restores the
  pre-video state. No partition was ever written.

## Upstream references

- Related MediaTek 5.10 source (reference only, not the exact tablet driver):
  https://github.com/MotorolaMobilityLLC/kernel-mtk/blob/4cbe43fe20dc19b9c5218553468efb3b868567c5/drivers/media/platform/mtk-vcu/mtk_vcu.c
- Chromium 153.0.8010.36 (`media/gpu/v4l2/`, `content/browser/gpu/gpu_process_host.cc`,
  `content/gpu/gpu_main.cc`) at https://github.com/chromium/chromium/tree/153.0.8010.36
- Bionic linker/libc references: https://android.googlesource.com/platform/bionic/
  (`android13-release`, `android16-release`)
- libyuv `MM21ToNV12` is used for detiling (system `libyuv` package).
