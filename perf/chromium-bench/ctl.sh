#!/bin/bash
# Benchmark Chromium on the tablet (root): ctl.sh start flush|finish | stop
set -u
B=/tmp/bench
S=$(ls -t /run/user/1001/hypr | head -1)
H() { runuser -u hypr -- env XDG_RUNTIME_DIR=/run/user/1001 HYPRLAND_INSTANCE_SIGNATURE=$S hyprctl "$@"; }
stop() {
  for p in $(pgrep -u hypr -x chromium); do
    tr '\0' ' ' < /proc/$p/cmdline 2>/dev/null | grep -q -- "--user-data-dir=$B/profile" && kill $p 2>/dev/null
  done
  sleep 2
}
case $1 in
  start)
    stop
    install -d -o hypr -g hypr $B
    rm -rf $B/profile
    src=/opt/omarchy-tablet/video/bin/chromium-gpu-launcher
    if [ "$2" = flush ]; then sed 's/ TABLET_CHROMIUM_GL_FINISH_ALWAYS=1//' $src > $B/launcher-$2
    else cp $src $B/launcher-$2; fi
    # GPU-process stderr (loader real-flush/real-finish counters) per mode.
    sed -i "2i exec 2>>$B/gpu-$2.log" $B/launcher-$2
    chmod 755 $B/launcher-$2
    extra=""; [ "${3:-}" = nogpu ] && extra="--disable-gpu"
    H dispatch "hl.dsp.exec_cmd(\"/opt/omarchy-tablet/gpu/app-env chromium --user-data-dir=$B/profile --remote-debugging-port=9333 --gpu-launcher=$B/launcher-$2 --no-first-run --no-default-browser-check $extra --new-window about:blank\")" >/dev/null
    for _ in $(seq 60); do curl -s 127.0.0.1:9333/json/version >/dev/null && break; sleep 0.5; done
    sleep 3
    ;;
  stop) stop ;;
esac
