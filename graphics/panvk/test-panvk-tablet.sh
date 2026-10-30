#!/bin/bash
# Run a single isolated probe on the tablet. Does not change desktop settings.
set -euo pipefail
icd=${PANVK_TEST_ICD:-/opt/omarchy-tablet/gpu/panvk/share/vulkan/icd.d/panfrost_icd.aarch64.json}
diag=/opt/omarchy-tablet/diagnostics/gpu
test -f "$icd"
case "${1:-}" in
  enumerate) probe=(vulkaninfo --summary) ;;
  compute) probe=("$diag/gpu-smoke" "$diag/gpu-smoke.spv") ;;
  gles) probe=("$diag/egl-smoke") ;;
  gbm) probe=("$diag/egl-smoke" gbm) ;;
  *) echo 'Usage: test-panvk-tablet.sh enumerate|compute|gles|gbm' >&2; exit 2 ;;
esac
exec env -u LIBGL_ALWAYS_SOFTWARE -u LD_LIBRARY_PATH -u LD_PRELOAD \
  -u MESA_LOADER_DRIVER_OVERRIDE -u VK_ICD_FILENAMES \
  -u MESA_GLES_VERSION_OVERRIDE -u MESA_GL_VERSION_OVERRIDE -u MESA_EXTENSION_OVERRIDE \
  VK_DRIVER_FILES="$icd" PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1 \
  PANVK_KBASE_USER_CACHE_SYNC=0 GALLIUM_DRIVER=zink GBM_ALWAYS_SOFTWARE=1 \
  timeout --signal=TERM --kill-after=5s 40s "${probe[@]}"
