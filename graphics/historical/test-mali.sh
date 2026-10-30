#!/bin/bash
# test-mali.sh - Arm libmali r32p0 (MediaTek Genio build) against the tablet's kbase r32p1 driver. Run ON the tablet as root.
#
#   scp gfx/libmali-mtk/mt8188/r32p0/usr/lib/libmali.so.0.32.0 gfx/test-mali.sh gfx/shim/egl-shim.so root@172.16.42.1:/tmp/gfx/
#   bash /tmp/gfx/test-mali.sh install      # builds /opt/libmali/lib symlink farm (libEGL/libGLESv2/libgbm/libwayland-egl -> libmali)
#   bash /tmp/gfx/test-mali.sh kbase        # checks /dev/mali0, kbase version, userspace/kernel handshake via a tiny probe
#   bash /tmp/gfx/test-mali.sh weston       # weston --backend=drm --renderer=gl on libmali (Genio's reference compositor)
#   bash /tmp/gfx/test-mali.sh hypr         # patched Hyprland on libmali (see PLAN.md for the patch/build)
#   bash /tmp/gfx/test-mali.sh stop
#
# 'hypr' runs the stock ALARM Hyprland with LD_PRELOAD=egl-shim.so (stubs eglDebugMessageControlKHR, which libmali lacks).
# Knobs: DURATION=40 HYPR_USER=hypr LIBMALI_SRC=/tmp/gfx/libmali.so.0.32.0 OUT=/tmp/gfx-test-mali HYPR_BIN=Hyprland
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/gfx-test-mali}; DURATION=${DURATION:-40}; HYPR_USER=${HYPR_USER:-hypr}
LIBMALI_SRC=${LIBMALI_SRC:-$HERE/libmali.so.0.32.0}; FARM=/opt/libmali/lib; HYPR_BIN=${HYPR_BIN:-Hyprland}
mkdir -p "$OUT"; chmod 777 "$OUT"
res() { echo "RESULT $1 $2 ${*:3}" | tee -a "$OUT/summary.txt"; }
[ "$(id -u)" = 0 ] || { echo "run as root"; exit 2; }

install_farm() {
  [ -f "$LIBMALI_SRC" ] || { res install FAIL "missing $LIBMALI_SRC"; return 1; }
  mkdir -p $FARM; install -m755 "$LIBMALI_SRC" $FARM/libmali.so.0.32.0
  cd $FARM || return 1
  for l in libmali.so.0 libmali.so libEGL.so.1 libEGL.so libGLESv2.so.2 libGLESv2.so libGLESv1_CM.so.1 libgbm.so.1 libgbm.so libwayland-egl.so.1 libwayland-egl.so; do ln -sf libmali.so.0.32.0 $l; done
  sha256sum libmali.so.0.32.0
  [ -f "$HERE/egl-shim.so" ] && install -m755 "$HERE/egl-shim.so" /opt/libmali/egl-shim.so
  # symbols the glibc userspace needs from the system
  LD_LIBRARY_PATH=$FARM ldd $FARM/libmali.so.0.32.0 | tee "$OUT/ldd.txt"
  grep -q "not found" "$OUT/ldd.txt" && res install FAIL "unresolved libs: $(grep 'not found' "$OUT/ldd.txt" | tr -d '\t' | tr '\n' ' ')" || res install PASS "$FARM"
}

kbase() {
  ls -l /dev/mali0 2>&1
  for f in /sys/module/mali_kbase*/version /sys/module/mali_kbase*/parameters/*; do [ -r "$f" ] && echo "$f: $(cat $f 2>/dev/null | head -1)"; done 2>/dev/null | head -20
  cat /sys/class/misc/mali0/device/gpuinfo 2>/dev/null || cat /sys/devices/platform/*gpu*/gpuinfo 2>/dev/null
  dmesg | grep -i -E "kbase|mali" | tail -15
  chmod 666 /dev/mali0 2>/dev/null   # test only; use a udev rule + 'render'/'video' group for real
  echo "(handshake is exercised by the weston step: libmali issues VERSION_CHECK 11.31 at EGL init;"
  echo " a mismatch shows as 'Unsupported UK version'/'Error: kbase_api_handshake' in dmesg and eglInitialize failing)"
}

services() {
  pgrep -x systemd-udevd >/dev/null || /usr/lib/systemd/systemd-udevd --daemon 2>/dev/null
  udevadm trigger --action=add 2>/dev/null; udevadm settle -t 10 2>/dev/null
  id $HYPR_USER >/dev/null 2>&1 || useradd -m -G video,input,render,seat $HYPR_USER 2>/dev/null || useradd -m -G video,input $HYPR_USER
  UID_N=$(id -u $HYPR_USER); mkdir -p /run/user/$UID_N; chown $HYPR_USER: /run/user/$UID_N; chmod 700 /run/user/$UID_N
  chmod 666 /dev/mali0 2>/dev/null
  pgrep -x seatd >/dev/null || { G=seat; getent group seat >/dev/null || G=video; seatd -g $G >"$OUT/seatd.log" 2>&1 & sleep 1; }
}

run_user() { runuser -u $HYPR_USER -- env XDG_RUNTIME_DIR=/run/user/$UID_N LIBSEAT_BACKEND=seatd LD_LIBRARY_PATH=$FARM LD_PRELOAD=/opt/omarchy-tablet/drm-no-cursor.so:/opt/libmali/egl-shim.so:$FARM/libmali.so.0.32.0 \
    EGL_LOG_LEVEL=debug MALI_LOG=1 "$@"; }

weston_test() {
  command -v weston >/dev/null || { res weston FAIL "pacman -S weston"; return 1; }
  [ -e $FARM/libEGL.so.1 ] || install_farm
  services; dmesg > "$OUT/dmesg-before-weston.txt" 2>/dev/null; dmesg --console-off 2>/dev/null
  (timeout $((DURATION+10)) bash -c "$(declare -f run_user); HYPR_USER=$HYPR_USER UID_N=$UID_N FARM=$FARM; run_user weston --backend=drm --renderer=gl --continue-without-input --socket=wl-mali --log=$OUT/weston.log" >"$OUT/weston.stdout" 2>&1 &)
  sleep 12
  if pgrep -x weston >/dev/null; then
    res weston-start PASS "running; checking renderer"
    grep -i -E "renderer|egl|gbm|dmabuf|gles|vendor|version|mali|output|atomic|modifier" "$OUT/weston.log" | head -40
    if command -v weston-simple-egl >/dev/null; then
      run_user env WAYLAND_DISPLAY=wl-mali timeout 12 weston-simple-egl > "$OUT/simple-egl.txt" 2>&1; tail -5 "$OUT/simple-egl.txt"
      grep -q "fps" "$OUT/simple-egl.txt" && res weston-egl-client PASS "$(tail -1 "$OUT/simple-egl.txt")" || res weston-egl-client FAIL "see $OUT/simple-egl.txt"
    fi
    if command -v weston-simple-dmabuf-egl >/dev/null; then
      run_user env WAYLAND_DISPLAY=wl-mali timeout 10 weston-simple-dmabuf-egl > "$OUT/simple-dmabuf.txt" 2>&1; tail -3 "$OUT/simple-dmabuf.txt"
    fi
  else
    res weston-start FAIL "see $OUT/weston.stdout $OUT/weston.log"; tail -30 "$OUT/weston.stdout"; tail -30 "$OUT/weston.log" 2>/dev/null
  fi
  echo "--- kernel"; dmesg | grep -i -E "kbase|mali|drm|mtk|iommu|fault|oops|BUG|warn" | tail -40 | tee "$OUT/weston-dmesg.txt"
  stop
}

hypr_test() {
  command -v $HYPR_BIN >/dev/null || { res hypr FAIL "install the patched Hyprland package first"; return 1; }
  [ -e $FARM/libEGL.so.1 ] || install_farm
  services
  cat > "$OUT/hypr-mali.lua" <<'EOF'
-- Hyprland on Arm libmali (GPU rendering)
hl.monitor({ output = "DSI-1", mode = "1200x1920@60", position = "0x0", scale = 1.5 })
hl.config({
  animations = { enabled = true },
  decoration = { rounding = 0, blur = { enabled = false }, shadow = { enabled = false } },
  misc = { disable_hyprland_logo = true, disable_splash_rendering = true },
  render = { use_fp16 = 0, cm_enabled = false, new_render_scheduling = true },
  cursor = { no_hardware_cursors = true },
  debug = { overlay = true, vfr = true },
  ecosystem = { no_update_news = true, no_donation_nag = true },
})
hl.on("hyprland.start", function() hl.exec_cmd("foot") end)
EOF
  chmod 644 "$OUT/hypr-mali.lua"
  (timeout $((DURATION+20)) bash -c "$(declare -f run_user); HYPR_USER=$HYPR_USER UID_N=$UID_N FARM=$FARM; run_user env AQ_DRM_DEVICES=/dev/dri/card0 AQ_TRACE=1 AQ_NO_MODIFIERS=\${NO_MODIFIERS:-0} $HYPR_BIN --config $OUT/hypr-mali.lua" >"$OUT/hypr.stdout" 2>&1 &)
  sleep 15
  pgrep -x $HYPR_BIN >/dev/null && res hypr-start PASS || { res hypr-start FAIL "see $OUT/hypr.stdout"; tail -40 "$OUT/hypr.stdout"; }
  LOG=$(ls -t /run/user/$UID_N/hypr/*/hyprland.log 2>/dev/null | head -1); [ -n "$LOG" ] && cp "$LOG" "$OUT/hyprland.log" && grep -E "Renderer:|Vendor:|Using:|EGL|gbm|native_fence|render node" "$OUT/hyprland.log" | head -30
  top -bn1 | head -12 | tee "$OUT/top.txt"
  sleep $DURATION; dmesg | grep -i -E "kbase|mali|fault|oops|BUG" | tail -20; stop
}

stop() { pkill -x $HYPR_BIN; pkill -x weston; pkill -x foot; sleep 1; pkill -x seatd; echo stopped; }

case "$1" in install) install_farm ;; kbase) kbase ;; weston) weston_test ;; hypr) hypr_test ;; stop) stop ;;
  *) echo "usage: $0 install|kbase|weston|hypr|stop"; exit 1 ;; esac
echo "== summary"; cat "$OUT/summary.txt" 2>/dev/null
