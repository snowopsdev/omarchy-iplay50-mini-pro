#!/usr/bin/env bash
# Host test: build the patched keyboard, then run the real keyboard state machine and
# Cairo/Pango renderer off-screen (only outbound Wayland messages are recorded).
# Renders 12 PNGs (every page, portrait + landscape) into touch-ui/build/test/ui/evidence/.
# Needs network on first run (git clone of upstream wvkbd v0.20) and the wvkbd build deps.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
SRC=${WVKBD_SRC:-$REPO/touch-ui/build/wvkbd}
OUT=$REPO/touch-ui/build/test
WVKBD_SRC=$SRC "$REPO/touch-ui/wvkbd/build.sh" host
mkdir -p "$OUT/ui/evidence"
cc -std=gnu99 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
  -DLAYOUT='"layout.mobintl.h"' -DKEYMAP='"keymap.mobintl.h"' \
  -I "$SRC" $(pkg-config --cflags wayland-client xkbcommon pangocairo) \
  "$REPO/touch-ui/tests/keyboard-test.c" "$SRC/keyboard.c" "$SRC/drw.c" \
  "$SRC/os-compatibility.c" "$SRC/shm_open.c" \
  "$SRC/proto/virtual-keyboard-unstable-v1-client-protocol.o" \
  -o "$OUT/keyboard-test" $(pkg-config --libs wayland-client xkbcommon pangocairo) -lm -lrt
# The test writes its renders to ./ui/evidence/ relative to the working directory.
(cd "$OUT" && ./keyboard-test)
bash -n "$REPO/touch-ui/rootfs/tablet-ui"
python3 -m py_compile "$REPO/touch-ui/rootfs/install-touch-ui.py"
echo "renders: $OUT/ui/evidence/"
