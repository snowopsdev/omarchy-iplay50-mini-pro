#!/bin/bash
# Run on the tablet from the source archive, outside any PID namespace.
# Builds into a private prefix; does not activate the driver for the desktop.
set -euo pipefail
cd "${PANVK_SRC:-/home/hypr/src/panvk-kbase}"
export CC=clang CXX=clang++
prefix=${PANVK_PREFIX:-/opt/omarchy-tablet/gpu/panvk-fourfect}
setup_args=()
if test -f build-tablet/meson-private/coredata.dat; then
  setup_args+=(--reconfigure)
fi
meson setup "${setup_args[@]}" build-tablet --buildtype=release \
  --prefix="$prefix" --libdir=lib \
  -Db_ndebug=true -Dbuild-tests=false -Ddisplay-info=disabled \
  -Degl=disabled -Denable-glcpp-tests=false -Dgallium-drivers= \
  -Dgbm=disabled -Dgles1=disabled -Dgles2=disabled -Dglx=disabled \
  -Dinstall-mesa-clc=false -Dinstall-precomp-compiler=false \
  -Dlibunwind=disabled -Dlmsensors=disabled -Dllvm=enabled \
  -Dmesa-clc=enabled -Dopengl=false -Dpanfrost-kmds=kbase \
  -Dpanfrost-rust=false -Dplatforms=wayland \
  -Dprecomp-compiler=enabled -Dshared-glapi=disabled -Dtools= \
  -Dvalgrind=disabled -Dvideo-codecs= -Dvulkan-drivers=panfrost \
  -Dvulkan-layers= -Dxmlconfig=disabled -Dzstd=disabled
nice -n 10 ninja -C build-tablet -j4
# Installation is a separate root step after successful build:
# meson install -C build-tablet --no-rebuild
