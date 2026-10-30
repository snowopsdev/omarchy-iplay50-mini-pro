#!/bin/sh
set -eu
runuser -u hypr -- env XDG_RUNTIME_DIR=/run/user/1001 \
  DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1001/bus \
  systemd-run --user --unit=tablet-video-browser-trial --collect \
  --property=StandardOutput=append:/home/hypr/.cache/tablet-video-browser-trial.log \
  --property=StandardError=append:/home/hypr/.cache/tablet-video-browser-trial.log \
  env WAYLAND_DISPLAY=wayland-1 DISPLAY=:0 LIBGL_ALWAYS_SOFTWARE=1 \
  GALLIUM_DRIVER=llvmpipe \
  LD_PRELOAD=/opt/omarchy-tablet/drm-no-cursor.so:/opt/omarchy-tablet/aq-software-primary.so \
  /usr/lib/chromium/chromium \
  --user-data-dir=/home/hypr/.cache/chromium-video-hw \
  --remote-debugging-address=127.0.0.1 --remote-debugging-port=9223 \
  --ozone-platform=wayland --use-gl=angle --use-angle=gles-egl --ignore-gpu-blocklist --disable-gpu-rasterization --disable-gpu-memory-buffer-video-frames \
  --disable-features=Vulkan,SkiaGraphite \
  --enable-features=AcceleratedVideoDecoder --no-unsandboxed-zygote \
  --gpu-launcher=/opt/omarchy-tablet/video/probes/chromium-gpu-video-trial \
  --no-first-run --no-default-browser-check --enable-logging=stderr \
  --vmodule=v4l2*=3,video_decoder_pipeline=3 \
  file:///opt/omarchy-tablet/diagnostics/browser-gpu/playback.html
