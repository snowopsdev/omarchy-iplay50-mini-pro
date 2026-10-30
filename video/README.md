# video/

Sources for native H264 hardware decoding in Chromium. Instructions, status
and lessons: [`../docs/09-video-decoding.md`](../docs/09-video-decoding.md).
The directory names mirror the original project so relative `#include`s work.

| Path in this repo | Installed on the tablet as | Purpose |
|---|---|---|
| `prepare-trial.sh` | `/opt/omarchy-tablet/video/prepare-trial.sh` | Read-only map of Android `system`, runtime APEX mount, ordered vendor module loads up to `mtk-vcu` |
| `hw-userspace/service/omarchy-tablet-vcu.service` | `/etc/systemd/system/` | Type=notify VCU runtime service |
| `hw-userspace/service/71-omarchy-tablet-vcu-root-only.rules` | `/etc/udev/rules.d/` | Decoder node root-only by default |
| `hw-userspace/service/{supervisor.py,runtime-inner.sh,native-arm-policy.py,browser-access.py}` | `/opt/omarchy-tablet/video/runtime/` | Namespace/chroot supervisor, binfmt policy, guarded access grant |
| `hw-userspace/ld.config.txt` | `/opt/omarchy-tablet/video/runtime/ld.config.txt` | Private Bionic linker config for the chroot |
| `hw-userspace/service/test-browser-access.py` | — | Offline tests (uses `evidence/guard-runtime-first-boot.txt`) |
| `hw-driver/compat/guard-mmap-module.py` | — | Creates `mtk-vcodec-dec-v1-dmabuf-only.ko` from YOUR stock module (2 instructions) |
| `hw-driver/compat/check-memory-guard.c` | `/opt/omarchy-tablet/video/probes/check-memory-guard` | Allocation-free live check of the guarded module |
| `hw-driver/v4l2-query.c` | `/opt/omarchy-tablet/video/probes/v4l2-query` | Non-streaming capability query (used by the supervisor) |
| `hw-driver/v4l2-decode.c`, `hw-driver/make-sample.py`, `hw-driver/check-frame.py` | diagnostics | Native all-DMABUF decode probe, fixture generator, pixel checker |
| `hw-driver/compat/mt6789-v4l2-bridge.c` + `hw-format/m21s-*.{c,h}` | `/opt/omarchy-tablet/video/lib/mt6789-v4l2-bridge.so` | LD_PRELOAD V4L2 compatibility bridge (Chromium GPU process only) |
| `hw-format/chromium-gl-finish-loader.c` | `/opt/omarchy-tablet/video/lib/chromium-gl-finish-loader.so` | glFlush→glFinish for Chromium's llvmpipe GPU process |
| `install/chromium-gpu-launcher` | `/opt/omarchy-tablet/video/bin/chromium-gpu-launcher` | `--gpu-launcher`: adds the two libraries only when decode is ready |
| `install/enable-browser.py` | run once as root | Edits `~hypr/.config/chromium-flags.conf` (with backup) |
| `youtube-h264/` | `/opt/omarchy-tablet/video/youtube-h264/` | MV3 extension: YouTube picks H264 |
| `hw-driver/compat/*-host-test.c`, `hw-format/test-finish-policy.py`, `hw-format/finish-harness/` | — | Offline (host) tests |
| `hw-driver/compat/{mmap-decode,argb-mmap-decode,drain-lifecycle-probe,native-retention-probe,color-query-probe}.c`, `check-argb-frame.py` | diagnostics | Bridge-level native probes |
| `hw-driver/mt6789-v4l2-compat.c` | — | Superseded first adapter (sentinel only); kept for history |
| `hw-userspace/{elf-closure.py,probe-runtime.sh}` | — | Bionic dependency closure / linker preflight |
| `install/{install-vcu-gate.sh,final-health.py,monitor-playback.py,start-normal-browser-check.sh}` | — | Install helper and read-only health/monitoring scripts actually used |
| `tools/` | — | Read-only decoder inventory, module dependency script, CDP measurement scripts |
| `trials/` | — | Launchers of the intermediate Chromium trials (history; not needed) |

Not included (obtain from your own device or upstream, see the doc): the stock
vendor modules and `v3avpud` daemon, the Android `system` image and runtime
APEX, the `system-ro.table` device-mapper table, the patched decoder module,
test media, disassembly listings and Chromium source excerpts.

Nothing here was re-tested after being copied. One edit: `hw-format/test-finish-policy.py`
paths now point at `video/` and `graphics/shim/` (its 14 host cases and the 8
`test-browser-access.py` cases still passed on an x86_64 host after the edit).
