# Licensing

Original work in this repository (documentation, scripts, configuration, and source files
without their own license header) is released under the MIT License in [LICENSE](LICENSE).
Some of it, for example `connectivity/src/mtk-bt-bridge.c` and `sensors/tablet-orientation`,
uses interface details (ioctl numbers, packet layouts) learned from MediaTek's GPL kernel
sources.

Exceptions, which keep the license of the code they derive from:

| Files | License | Why |
|---|---|---|
| `connectivity/src/mtk-wmt-init.c` | GPL-2.0 | Marked in the file: ioctl numbers derived from MediaTek's GPL kernel headers |
| `power/src/fgd-kernel-mode.c` | GPL-2.0-only | Protocol structures derived from the GPL vendor gauge driver |
| `kernel/omarchy-tablet.config`, `kernel/config-vs-stock.diff` | GPL-2.0 | Linux kernel configuration |
| `touch-ui/wvkbd/tablet-wvkbd-v0.20.patch`, `touch-ui/tests/keyboard-test.c` | GPL-3.0 | Patch against / tests linking [wvkbd](https://github.com/jjsullivan5196/wvkbd) |
| `graphics/panvk/patches/*.patch`, `graphics/browser-gpu/*.patch` | MIT | Patches against Mesa |
| `graphics/historical/patches/*.patch` | BSD-3-Clause | Patch against Hyprland |
| `omarchy/staged/prepare-runtime.py` | MIT | Embeds and patches snippets of Omarchy's QML |
| `shell-plugin/design/omarchy-touch-review.html` | Icons ISC (Lucide, license inline) | Embedded icon set |
| `video/hw-format/m21s-convert.c` | MIT | Header in file |

No third-party source trees, vendor binaries or firmware are included. Build scripts download
upstream sources at pinned commits, each under its own license (Linux and busybox GPL-2.0,
Mesa MIT, Hyprland BSD-3-Clause, wvkbd GPL-3.0, mkbootimg Apache-2.0, musl MIT). Vendor files
(MediaTek/Alldocube) are copied by each user from their own device and are not
redistributed here.

Product names (Alldocube, MediaTek, Helio, Mali, Omarchy, Chromium, YouTube) belong to their
owners; this project is not affiliated with them.
