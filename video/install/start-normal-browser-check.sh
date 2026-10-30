#!/bin/sh
set -eu
runuser -u hypr -- env XDG_RUNTIME_DIR=/run/user/1001 DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1001/bus \
 systemd-run --user --unit=tablet-video-normal-check --collect \
 --property=StandardOutput=append:/home/hypr/.cache/tablet-video-normal-check.log \
 --property=StandardError=append:/home/hypr/.cache/tablet-video-normal-check.log \
 env WAYLAND_DISPLAY=wayland-1 DISPLAY=:0 GALLIUM_DRIVER=llvmpipe LIBGL_ALWAYS_SOFTWARE=1 \
 LD_PRELOAD=/opt/omarchy-tablet/drm-no-cursor.so:/opt/omarchy-tablet/aq-software-primary.so \
 /usr/bin/chromium --remote-debugging-address=127.0.0.1 --remote-debugging-port=9223 \
 file:///opt/omarchy-tablet/diagnostics/browser-gpu/playback.html
