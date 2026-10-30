#!/bin/bash
# kernel/try-option.sh OPTION...: check whether extra config options keep the module ABI.
# For each OPTION: copy the configured tree's .config (run `kernel/build.sh fetch config` first), enable just that
# option, olddefconfig, full LTO+CFI build of Image+modules, then compare vmlinux.symvers with the CI one.
# Any "changed" CRC means the stock vendor modules would no longer load. Results: $WORK/results/opt_<OPTION>.*
# (Adapted from the project's kernel/scripts/opt.sh; each build takes as long as a full kernel build.)
#   e.g. kernel/try-option.sh VT SYSVIPC POSIX_MQUEUE PID_NS USER_NS
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
WORK=${WORK:-$REPO/work/kernel}
SRC=${SRC:-$WORK/src}
CLANG=${CLANG:-$WORK/clang/r416183b}
JOBS=${JOBS:-$(nproc)}
export PATH=$CLANG/bin:$PATH TZ=UTC LC_ALL=C ARCH=arm64
export SOURCE_DATE_EPOCH=$(git -C "$SRC" log -1 --pretty=%ct)
export KBUILD_BUILD_TIMESTAMP="Tue Mar  7 19:36:52 UTC 2023" KBUILD_BUILD_HOST=build-host KBUILD_BUILD_USER=build-user
export KBUILD_BUILD_VERSION=1 BUILD_NUMBER=9703858 KCFLAGS="-D__ANDROID_COMMON_KERNEL__"
TOOL_ARGS=(LLVM=1 LLVM_IAS=1 DEPMOD=depmod DTC=dtc CROSS_COMPILE=aarch64-linux-gnu-)
R=$WORK/results; mkdir -p "$R"
for opt in "$@"; do
	O=$WORK/out-opt_$opt; rm -rf "$O"; mkdir -p "$O"; cp "$WORK/out/.config" "$O/.config"
	"$SRC/scripts/config" --file "$O/.config" -e "$opt"
	make -C "$SRC" O="$O" "${TOOL_ARGS[@]}" olddefconfig > /dev/null 2>&1
	diff "$WORK/out/.config" "$O/.config" > "$R/opt_$opt.config.diff" || true
	if make -C "$SRC" O="$O" "${TOOL_ARGS[@]}" -j"$JOBS" Image modules > "$R/opt_$opt.log" 2>&1; then
		python3 -I "$HERE/abicmp.py" "$WORK/ci/vmlinux.symvers" "$O/vmlinux.symvers" > "$R/opt_$opt.abi.txt" 2>&1 || true
	else
		echo "BUILD FAILED $opt (see $R/opt_$opt.log)" > "$R/opt_$opt.abi.txt"
	fi
	echo "$opt: $(head -n1 "$R/opt_$opt.abi.txt")"
	rm -rf "$O"
done
