#!/usr/bin/env bash
# Build the MediaTek r32p0 allocator against our exact GKI configuration/CRCs.
# HISTORICAL: only needed for the abandoned libmali route (docs/08-gpu.md).
#
# Adapted from the private layout. Not re-tested after adaptation. Inputs:
#   KSRC        GKI kernel source tree used for the tablet kernel
#   KCONFIG     that kernel's .config
#   KSYMVERS    that kernel's vmlinux.symvers (exported CRCs)
#   TOOL_ARGS   make arguments selecting the same clang/lld toolchain
#               (e.g. "LLVM=1 ARCH=arm64 CC=... LD=..."), as used for the kernel
# Copy dma-buf-test-exporter.c (GPL-2.0, Arm) from the dma_buf_test_exporter
# base module of https://gitlab.com/mediatek/aiot/bsp/mtk-mali-gpu-driver.git
# (branch r32p0) next to this script, and its dma-buf-test-exporter.h UAPI
# header to include/linux/. Exact in-repo paths were not recorded.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
: "${KSRC:?set KSRC}" "${KCONFIG:?set KCONFIG}" "${KSYMVERS:?set KSYMVERS}"
read -r -a TOOL_ARGS <<< "${TOOL_ARGS:-}"
PREP=${PREP:-$HERE/kernel-prepare}
mkdir -p "$PREP"
cp "$KCONFIG" "$PREP/.config"
make -C "$KSRC" O="$PREP" "${TOOL_ARGS[@]}" -j8 modules_prepare
cp "$KSYMVERS" "$PREP/Module.symvers"
make -C "$KSRC" O="$PREP" M="$HERE" "${TOOL_ARGS[@]}" -j8 modules
sha256sum "$HERE/dma-buf-test-exporter.ko"
