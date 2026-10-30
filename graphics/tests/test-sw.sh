#!/bin/bash
# test-sw.sh - software-rendering fallback test for the tablet (run ON the tablet, as root, over SSH/telnet).
#
#   scp -r gfx/test-sw.sh gfx/kmstest/kmstest root@172.16.42.1:/tmp/gfx/     (or push however you reach it)
#   ssh root@172.16.42.1 'bash /tmp/gfx/test-sw.sh preflight'
#   ssh root@172.16.42.1 'bash /tmp/gfx/test-sw.sh kms'     # raw KMS: no packages needed (uses ./kmstest)
#   ssh root@172.16.42.1 'bash /tmp/gfx/test-sw.sh hypr'    # Hyprland on llvmpipe (needs packages, see PLAN.md)
#   ssh root@172.16.42.1 'bash /tmp/gfx/test-sw.sh stop'    # kill leftovers
#
# Knobs (environment): DURATION=45  LP_THREADS=2  PIN=big|all  HYPR_USER=hypr  NO_MODIFIERS=0|1
#                      HYPR_BIN=Hyprland  OUT=/tmp/gfx-test
# Every step prints "RESULT <name> PASS|WARN|FAIL <detail>"; full logs land in $OUT. Nothing here writes to
# flash/partitions; it only touches /tmp, /run, creates the unprivileged user $HYPR_USER, and starts seatd/udevd.

HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-/tmp/gfx-test}
DURATION=${DURATION:-45}
HYPR_USER=${HYPR_USER:-hypr}
LP_THREADS=${LP_THREADS:-2}
PIN=${PIN:-big}
NO_MODIFIERS=${NO_MODIFIERS:-0}
NO_ATOMIC=${NO_ATOMIC:-1}
HYPR_BIN=${HYPR_BIN:-Hyprland}
KMSTEST=${KMSTEST:-$HERE/kmstest}
[ -x "$KMSTEST" ] || KMSTEST=$HERE/kmstest/kmstest
mkdir -p "$OUT"

res() { echo "RESULT $1 $2 ${*:3}" | tee -a "$OUT/summary.txt"; }
need_root() { [ "$(id -u)" = 0 ] || { echo "run as root"; exit 2; }; }

big_cpus() {   # CPUs with the highest cpu_capacity (A76 cores on MT6789)
  local max=0 c v list=""
  for c in /sys/devices/system/cpu/cpu[0-9]*; do
    v=$(cat "$c/cpu_capacity" 2>/dev/null || echo 0); [ "$v" -gt "$max" ] && max=$v
  done
  [ "$max" -eq 0 ] && return 1
  for c in /sys/devices/system/cpu/cpu[0-9]*; do
    v=$(cat "$c/cpu_capacity" 2>/dev/null || echo 0)
    [ "$v" -eq "$max" ] && list="$list,${c##*cpu}"
  done
  echo "${list#,}"
}

dmesg_mark() { dmesg 2>/dev/null | wc -l > "$OUT/.dmesg_mark"; }
dmesg_new()  { local n; n=$(cat "$OUT/.dmesg_mark" 2>/dev/null || echo 0); dmesg 2>/dev/null | tail -n +$((n+1)) | grep -i -E "drm|mtk|mali|kbase|dsi|disp|iommu|oops|bug|warn|fail|error" | tail -n 60; }

preflight() {
  need_root
  { echo "== uname"; uname -a
    echo "== cpus (capacity)"; for c in /sys/devices/system/cpu/cpu[0-9]*; do echo "${c##*/} cap=$(cat $c/cpu_capacity 2>/dev/null) max=$(cat $c/cpufreq/cpuinfo_max_freq 2>/dev/null) gov=$(cat $c/cpufreq/scaling_governor 2>/dev/null)"; done
    echo "== /dev/dri"; ls -l /dev/dri /dev/mali0 /dev/dma_heap 2>&1
    echo "== modules"; lsmod 2>/dev/null | grep -i -E "mediatek|mtk_drm|mali|panfrost|drm" | head -20
    echo "== mesa dri drivers"; ls -l /usr/lib/dri/ 2>&1 | head -20; ls /usr/lib/libgallium* /usr/lib/libEGL* /usr/lib/libgbm* 2>&1 | head
    echo "== packages"; pacman -Q hyprland aquamarine mesa libdrm seatd foot grim weston libglvnd llvm-libs 2>&1
    echo "== tools"; for t in $HYPR_BIN hyprctl seatd udevadm foot grim weston-simple-shm eglinfo modetest drm_info taskset runuser; do printf '%-18s %s\n' $t "$(command -v $t || echo MISSING)"; done
    echo "== backlight"; ls /sys/class/leds/lcd-backlight 2>&1 | head -3
    echo "== connector status"; for s in /sys/class/drm/card0-*/status; do echo "$s $(cat $s)"; done
  } > "$OUT/preflight.txt" 2>&1
  cat "$OUT/preflight.txt"
  [ -e /dev/dri/card0 ] && res preflight-card0 PASS || res preflight-card0 FAIL "/dev/dri/card0 missing (load mediatek-drm first)"
  [ -x "$KMSTEST" ] && res preflight-kmstest PASS "$KMSTEST" || res preflight-kmstest WARN "kmstest binary not found next to script"
}

kms() {
  need_root
  [ -x "$KMSTEST" ] || { res kms FAIL "no kmstest binary"; return 1; }
  pkill -x "$HYPR_BIN" 2>/dev/null; sleep 1
  dmesg_mark
  "$KMSTEST" info > "$OUT/kms-info.txt" 2>&1; rc=$?
  [ $rc = 0 ] && res kms-info PASS "see $OUT/kms-info.txt" || res kms-info FAIL "rc=$rc"
  grep -E "^(driver|cap |client cap|connector|    mode)" "$OUT/kms-info.txt" | head -30

  "$KMSTEST" legacy 5 > "$OUT/kms-legacy.txt" 2>&1; rc=$?; cat "$OUT/kms-legacy.txt"
  [ $rc = 0 ] && res kms-legacy PASS "color bars + orange frame for 5 s (confirm visually)" || res kms-legacy FAIL "rc=$rc (EACCES/EBUSY => someone else is DRM master: fbcon/other compositor)"

  "$KMSTEST" flip 180 > "$OUT/kms-flip.txt" 2>&1; rc=$?; cat "$OUT/kms-flip.txt"
  [ $rc = 0 ] && res kms-flip PASS "$(grep '^flip:' "$OUT/kms-flip.txt")" || res kms-flip FAIL "rc=$rc"

  "$KMSTEST" atomic 5 > "$OUT/kms-atomic.txt" 2>&1; rc=$?; cat "$OUT/kms-atomic.txt"
  [ $rc = 0 ] && res kms-atomic PASS "$(grep -c '^OK' "$OUT/kms-atomic.txt") OK lines, $(grep -c '^WARN' "$OUT/kms-atomic.txt") WARN" || res kms-atomic FAIL "rc=$rc"

  for h in system system-uncached mtk_mm; do
    [ -e /dev/dma_heap/$h ] || continue
    "$KMSTEST" heap $h 4 > "$OUT/kms-heap-$h.txt" 2>&1; rc=$?; cat "$OUT/kms-heap-$h.txt"
    [ $rc = 0 ] && res kms-heap-$h PASS "dma-heap buffer imported (PRIME) and scanned out" || res kms-heap-$h FAIL "rc=$rc"
  done

  "$KMSTEST" bw > "$OUT/kms-bw.txt" 2>&1; cat "$OUT/kms-bw.txt"
  res kms-bw PASS "see $OUT/kms-bw.txt (dumb-buffer READ speed predicts llvmpipe blend cost)"
  echo "--- kernel messages during kms tests"; dmesg_new | tee "$OUT/kms-dmesg.txt"
}

ensure_services() {
  # udev database is required for aquamarine/libinput device discovery
  if ! pgrep -x systemd-udevd >/dev/null; then
    (command -v /usr/lib/systemd/systemd-udevd >/dev/null && /usr/lib/systemd/systemd-udevd --daemon) 2>/dev/null
  fi
  udevadm trigger --action=add 2>/dev/null; udevadm settle -t 10 2>/dev/null
  id "$HYPR_USER" >/dev/null 2>&1 || useradd -m -G video,input,render,seat "$HYPR_USER" 2>/dev/null || useradd -m -G video,input "$HYPR_USER"
  for g in video input render seat; do getent group $g >/dev/null && usermod -aG $g "$HYPR_USER" 2>/dev/null; done
  UID_N=$(id -u "$HYPR_USER")
  mkdir -p /run/user/$UID_N; chown "$HYPR_USER": /run/user/$UID_N; chmod 700 /run/user/$UID_N
  if ! pgrep -x seatd >/dev/null; then
    G=seat; getent group seat >/dev/null || G=video
    seatd -g $G >"$OUT/seatd.log" 2>&1 &
    sleep 1
  fi
}

write_conf() {
  cat > "$OUT/hypr-sw.lua" <<'EOF'
-- low-load config for software rendering (Hyprland >= 0.55 uses Lua)
hl.monitor({ output = "DSI-1", mode = "1200x1920@60", position = "0x0", scale = 1.5 })
hl.config({
  animations = { enabled = false },
  decoration = { rounding = 0, blur = { enabled = false }, shadow = { enabled = false }, dim_inactive = false },
  general = { gaps_in = 0, gaps_out = 0, border_size = 1, allow_tearing = false },
  misc = { disable_hyprland_logo = true, disable_splash_rendering = true, force_default_wallpaper = 0, background_color = 0xff202020 },
  render = { direct_scanout = 0, use_fp16 = 0, cm_enabled = false, new_render_scheduling = true, xp_mode = true },
  cursor = { no_hardware_cursors = true },
  debug = { overlay = true, damage_tracking = 2, vfr = true },
  ecosystem = { no_update_news = true, no_donation_nag = true },
})
hl.on("hyprland.start", function() hl.exec_cmd("foot") end)
EOF
  chmod 644 "$OUT/hypr-sw.lua"
}

hypr() {
  need_root
  command -v "$HYPR_BIN" >/dev/null || { res hypr FAIL "$HYPR_BIN not installed (pacman -S hyprland seatd foot)"; return 1; }
  [ -f /opt/omarchy-tablet/drm-no-cursor.so ] || { res hypr FAIL "install drm-no-cursor.so first: the vendor cursor ioctl resets this tablet"; return 1; }
  pkill -x "$HYPR_BIN" 2>/dev/null; sleep 1
  ensure_services; write_conf; dmesg_mark
  BIG=$(big_cpus); PINCMD=""
  [ "$PIN" = big ] && [ -n "$BIG" ] && command -v taskset >/dev/null && PINCMD="taskset -c $BIG"
  echo "big cpus: ${BIG:-unknown}; pin cmd: ${PINCMD:-none}; LP_NUM_THREADS=$LP_THREADS"
  chmod 777 "$OUT"
  ENVV="XDG_RUNTIME_DIR=/run/user/$UID_N LIBSEAT_BACKEND=seatd HOME=/home/$HYPR_USER XDG_SESSION_TYPE=wayland"
  ENVV="$ENVV AQ_DRM_DEVICES=/dev/dri/card0 AQ_TRACE=1 HYPRLAND_TRACE=1"
  ENVV="$ENVV LD_PRELOAD=/opt/omarchy-tablet/drm-no-cursor.so AQ_NO_ATOMIC=$NO_ATOMIC"
  # Mesa: skip 'mediatek' + zink probing and go straight to kms_swrast (llvmpipe rendering into KMS dumb buffers)
  ENVV="$ENVV GBM_ALWAYS_SOFTWARE=1 LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe LP_NUM_THREADS=$LP_THREADS"
  [ "$NO_MODIFIERS" = 1 ] && ENVV="$ENVV AQ_NO_MODIFIERS=1"
  # shellcheck disable=SC2086
  nohup timeout $((DURATION+25)) runuser -u "$HYPR_USER" -- env $ENVV $PINCMD "$HYPR_BIN" --config "$OUT/hypr-sw.lua" \
      > "$OUT/hypr.stdout" 2>&1 &
  HP=$!
  echo "started (wrapper pid $HP); waiting for compositor..."
  for i in $(seq 1 40); do
    sleep 1
    SIG=$(ls -t /run/user/$UID_N/hypr 2>/dev/null | head -1)
    [ -n "$SIG" ] && [ -S "/run/user/$UID_N/hypr/$SIG/.socket.sock" ] && break
  done
  if [ -z "$SIG" ] || ! pgrep -x "$HYPR_BIN" >/dev/null; then
    res hypr-start FAIL "compositor not running; see $OUT/hypr.stdout and hyprland.log below"
    tail -40 "$OUT/hypr.stdout"
    cp /run/user/$UID_N/hypr/*/hyprland.log "$OUT/" 2>/dev/null; tail -60 "$OUT"/hyprland.log 2>/dev/null
    return 1
  fi
  res hypr-start PASS "signature $SIG"
  HC="runuser -u $HYPR_USER -- env XDG_RUNTIME_DIR=/run/user/$UID_N HYPRLAND_INSTANCE_SIGNATURE=$SIG hyprctl"
  sleep 5
  $HC version > "$OUT/hypr-version.txt" 2>&1
  $HC monitors > "$OUT/hypr-monitors.txt" 2>&1; cat "$OUT/hypr-monitors.txt"
  grep -q "DSI-1" "$OUT/hypr-monitors.txt" && res hypr-monitor PASS "$(grep -m1 -E '[0-9]+x[0-9]+@' "$OUT/hypr-monitors.txt")" || res hypr-monitor FAIL "no DSI-1 monitor"
  # load while idle, then with a client animating
  sleep 5; top -bn1 | head -15 > "$OUT/top-idle.txt"; cat "$OUT/top-idle.txt"
  if command -v weston-simple-shm >/dev/null; then
    runuser -u "$HYPR_USER" -- env XDG_RUNTIME_DIR=/run/user/$UID_N WAYLAND_DISPLAY=wayland-1 timeout 12 weston-simple-shm > "$OUT/simple-shm.txt" 2>&1 &
    sleep 8; top -bn1 | head -15 > "$OUT/top-busy.txt"; cat "$OUT/top-busy.txt"; sleep 5
    tail -3 "$OUT/simple-shm.txt"
  fi
  command -v grim >/dev/null && runuser -u "$HYPR_USER" -- env XDG_RUNTIME_DIR=/run/user/$UID_N WAYLAND_DISPLAY=wayland-1 grim "$OUT/shot.png" 2>"$OUT/grim.err" && res hypr-screenshot PASS "$OUT/shot.png"
  # extract renderer facts
  LOG=$(ls -t /run/user/$UID_N/hypr/*/hyprland.log | head -1); cp "$LOG" "$OUT/hyprland.log"
  grep -E "Renderer:|Vendor:|Using:|EGL:|gbm|GBM|render node|no render|Supported EGL|framerate|Atomic|modifier" "$OUT/hyprland.log" | head -40
  pgrep -x "$HYPR_BIN" >/dev/null && res hypr-alive PASS "still running after test" || res hypr-alive FAIL "compositor died; see $OUT/hyprland.log"
  echo "--- kernel messages"; dmesg_new | tee "$OUT/hypr-dmesg.txt"
  echo "leaving Hyprland running until timeout (~$((DURATION))s) - 'bash $0 stop' kills it"
  sleep "$DURATION"; stop
}

stop() {
  pkill -x "$HYPR_BIN" 2>/dev/null; pkill -x weston-simple-shm 2>/dev/null; pkill -x foot 2>/dev/null
  sleep 1; pkill -x seatd 2>/dev/null
  echo stopped
}

case "${1:-}" in
  preflight) preflight ;;
  kms) kms ;;
  hypr) hypr ;;
  stop) stop ;;
  all) preflight; kms; hypr ;;
  *) echo "usage: $0 preflight|kms|hypr|stop|all"; exit 1 ;;
esac
echo; echo "== summary"; cat "$OUT/summary.txt" 2>/dev/null
