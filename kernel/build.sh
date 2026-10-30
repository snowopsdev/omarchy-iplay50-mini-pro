#!/bin/bash
# kernel/build.sh: rebuild Google's GKI kernel build ab9703858 (5.10.149-android12-9-00031-g8d44cb1a9e42-ab9703858)
# with only module-ABI-safe config additions (omarchy-tablet.config), the stage0 initramfs built in, and
# rdinit=/linuxinit appended to CONFIG_CMDLINE.
#
#   kernel/build.sh [fetch] [config] [build] [verify] [dist]      (no argument = all five, in order)
#
# Env:
#   WORK=<dir>          work area (default: <repo>/work/kernel; needs ~3.5 GB for out/, ~2 GB for clang + src)
#   INITRAMFS=<cpio>    uncompressed newc cpio for CONFIG_INITRAMFS_SOURCE
#                       (default: <repo>/work/initramfs/out/initramfs-builtin.cpio, from initramfs/build-initramfs.py)
#   STOCK_CONFIG=<file> the device's own config (zcat /proc/config.gz). Default: regenerate it from the source with
#                       gki_defconfig + TRIM_UNUSED_KSYMS (checked identical to the device's config on 2026-10-30).
#   JOBS=<n>            make -j (default: nproc)
#   SRC=, CLANG=        reuse an existing kernel/common checkout or clang-r416183b directory
#
# Derived from the project's kernel/scripts/{env.sh,build.sh} and kernel/NOTES.md. The full sequence
# (fetch -> dist) was run step by step during the project; this consolidated script itself has been run on the
# desktop only up to and including "config" (output identical to the config that was booted).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
WORK=${WORK:-$REPO/work/kernel}
SRC=${SRC:-$WORK/src}
CLANG=${CLANG:-$WORK/clang/r416183b}
O=$WORK/out
CI=$WORK/ci
DIST=$WORK/dist
INITRAMFS=${INITRAMFS:-$REPO/work/initramfs/out/initramfs-builtin.cpio}
JOBS=${JOBS:-$(nproc)}

# ---- exact pins (manifest_9703858.xml)
COMMON_REV=8d44cb1a9e4253fe215bcc16435564f8d2e42333        # kernel/common, branch android12-5.10-2022-12
CLANG_REV=6e3223f76384455acde43affde3df0ea9df66c0d         # platform/prebuilts/clang/host/linux-x86
CLANG_URL=https://android.googlesource.com/platform/prebuilts/clang/host/linux-x86/+archive/$CLANG_REV/clang-r416183b.tar.gz
CI_URL=https://ci.android.com/builds/submitted/9703858/kernel_aarch64/latest/raw
SCMVERSION=-android12-9-00031-g8d44cb1a9e42                # what `git describe` gives on the full history
# sha256 of the CI artifacts as downloaded on 2026-10-27
CI_SUMS="d9e16b693c2ce0743b1f53c8c490c4e2079ea246f1147db69c3e55596e5d0479  vmlinux.symvers
9a62c7ef03f6fecfebfa40d129769c3ba20f4f05d3f0aba700c3050bc045854f  abi_symbollist
7a371b8edf0327e25439ec1b5c7be7b16fabd5e5bae732c278209c457a0b3faa  Image"
STOCK_BANNER='Linux version 5.10.149-android12-9-00031-g8d44cb1a9e42-ab9703858 (build-user@build-host) (Android (7284624, based on r416183b) clang version 12.0.5'

# ---- build environment (mirrors kernel/build _setup_env.sh + build.config.* for this branch)
setup_env() {
	export PATH=$CLANG/bin:$PATH
	export TZ=UTC LC_ALL=C
	export SOURCE_DATE_EPOCH=$(git -C "$SRC" log -1 --pretty=%ct)
	export KBUILD_BUILD_TIMESTAMP="Tue Mar  7 19:36:52 UTC 2023"   # stock banner date -> banner byte-identical
	export KBUILD_BUILD_HOST=build-host KBUILD_BUILD_USER=build-user KBUILD_BUILD_VERSION=1
	export BUILD_NUMBER=9703858                                     # appends -ab9703858 to UTS_RELEASE
	export KCFLAGS="-D__ANDROID_COMMON_KERNEL__"
	export ARCH=arm64
	TOOL_ARGS=(LLVM=1 LLVM_IAS=1 DEPMOD=depmod DTC=dtc CROSS_COMPILE=aarch64-linux-gnu-)
}
mk() { make -C "$SRC" O="$O" "${TOOL_ARGS[@]}" "$@"; }

step_fetch() {
	mkdir -p "$WORK" "$CI"
	if [ ! -e "$SRC/Makefile" ]; then
		git init -q "$SRC"
		git -C "$SRC" remote add origin https://android.googlesource.com/kernel/common
		git -C "$SRC" fetch --depth 1 origin $COMMON_REV
		git -C "$SRC" checkout -q FETCH_HEAD
	fi
	[ "$(git -C "$SRC" rev-parse HEAD)" = $COMMON_REV ] || { echo "src is not at $COMMON_REV" >&2; exit 1; }
	# shallow clone: setlocalversion cannot run git describe, so give it the stock string verbatim
	printf -- '%s' "$SCMVERSION" > "$SRC/.scmversion"
	if [ ! -x "$CLANG/bin/clang" ]; then
		mkdir -p "$CLANG"
		curl -fL "$CLANG_URL" -o "$WORK/clang-r416183b.tar.gz"
		tar -xzf "$WORK/clang-r416183b.tar.gz" -C "$CLANG"
		rm -f "$WORK/clang-r416183b.tar.gz"
	fi
	"$CLANG/bin/clang" --version | head -n1
	"$CLANG/bin/clang" --version | grep -q 'Android (7284624, based on r416183b) clang version 12.0.5' \
		|| { echo "unexpected clang version" >&2; exit 1; }
	for f in vmlinux.symvers abi_symbollist Image; do
		[ -s "$CI/$f" ] || curl -fL "$CI_URL/$f" -o "$CI/$f"
	done
	(cd "$CI" && echo "$CI_SUMS" | sha256sum -c -)
}

step_config() {
	setup_env
	mkdir -p "$O"
	[ -f "$INITRAMFS" ] || { echo "INITRAMFS=$INITRAMFS missing (run initramfs/build-initramfs.py first)" >&2; exit 1; }
	# KMI symbol list for TRIM_UNUSED_KSYMS: from the in-tree lists; must equal the CI artifact's flattened list
	python3 -I "$HERE/symbollist.py" "$SRC" > "$WORK/abi_symbollist.raw"
	python3 -I "$HERE/symbollist.py" --file "$CI/abi_symbollist" | cmp - "$WORK/abi_symbollist.raw"
	echo "symbol list: $(wc -l < "$WORK/abi_symbollist.raw") symbols, identical to CI"
	# stock config
	if [ -n "${STOCK_CONFIG:-}" ]; then
		cp "$STOCK_CONFIG" "$O/.config"
	else
		mk gki_defconfig > /dev/null
		"$SRC/scripts/config" --file "$O/.config" -e TRIM_UNUSED_KSYMS \
			--set-str UNUSED_KSYMS_WHITELIST /buildbot/src/android/common-android12-5.10-2022-12/out/android12-5.10/common/abi_symbollist.raw
		mk olddefconfig > /dev/null
	fi
	cp "$O/.config" "$WORK/stock-config"
	# additions
	local en=() cmdline
	for c in $(sed -n 's/^CONFIG_\([A-Z0-9_]*\)=y$/\1/p' "$HERE/omarchy-tablet.config"); do en+=(-e "$c"); done
	cmdline=$(sed -n 's/^CONFIG_CMDLINE="\(.*\)"$/\1/p' "$WORK/stock-config")
	"$SRC/scripts/config" --file "$O/.config" "${en[@]}" \
		--set-str UNUSED_KSYMS_WHITELIST "$WORK/abi_symbollist.raw" \
		--set-str INITRAMFS_SOURCE "$(readlink -f "$INITRAMFS")" \
		--set-str CMDLINE "$cmdline rdinit=/linuxinit"
	mk olddefconfig > /dev/null
	# the result must differ from stock in exactly the expected lines
	diff "$WORK/stock-config" "$O/.config" \
		| sed -E 's#^(> CONFIG_INITRAMFS_SOURCE=).*#\1"<initramfs-builtin.cpio>"#; s#^(> CONFIG_UNUSED_KSYMS_WHITELIST=).*#\1"<abi_symbollist.raw>"#' \
		> "$WORK/config-vs-stock.diff" || true
	if cmp -s "$WORK/config-vs-stock.diff" "$HERE/config-vs-stock.diff"; then
		echo "config: differs from stock exactly as kernel/config-vs-stock.diff"
	else
		echo "WARNING: config diff differs from kernel/config-vs-stock.diff:" >&2
		diff "$HERE/config-vs-stock.diff" "$WORK/config-vs-stock.diff" >&2 || true
	fi
}

step_build() {
	setup_env
	# Kbuild tracks the built-in cpio only through its path: force the initramfs objects to be rebuilt
	rm -f "$O/usr/initramfs_data.o" "$O/usr/initramfs_inc_data"
	echo "building (log: $WORK/build.log) ..."
	mk -j"$JOBS" Image Image.lz4 modules > "$WORK/build.log" 2>&1 || { tail -n 30 "$WORK/build.log"; exit 1; }
	echo BUILD_DONE >> "$WORK/build.log"
}

step_verify() {
	setup_env
	echo "== release: $(mk -s kernelrelease) / utsrelease.h: $(sed -n 's/.*UTS_RELEASE "\(.*\)"/\1/p' "$O/include/generated/utsrelease.h")"
	strings "$O/arch/arm64/boot/Image" | grep -m1 '^Linux version 5' | grep -qF "$STOCK_BANNER" \
		&& echo "== banner matches stock" || { echo "banner differs from stock" >&2; exit 1; }
	echo "== ABI vs CI:"
	python3 -I "$HERE/abicmp.py" "$CI/vmlinux.symvers" "$O/vmlinux.symvers"
	cmp "$CI/vmlinux.symvers" "$O/vmlinux.symvers" && echo "== vmlinux.symvers byte-identical to CI"
	if [ -f "$O/usr/initramfs_inc_data" ]; then
		[ "$(gzip -dc "$O/usr/initramfs_inc_data" | sha256sum | cut -d' ' -f1)" = "$(sha256sum < "$INITRAMFS" | cut -d' ' -f1)" ] \
			&& echo "== built-in initramfs == $INITRAMFS" || { echo "built-in initramfs differs from $INITRAMFS" >&2; exit 1; }
	fi
}

step_dist() {
	mkdir -p "$DIST"
	cp "$O/arch/arm64/boot/Image" "$O/arch/arm64/boot/Image.lz4" "$O/vmlinux.symvers" "$O/System.map" "$DIST/"
	cp "$O/.config" "$DIST/config"
	gzip -9 -n -c "$O/arch/arm64/boot/Image" > "$DIST/Image.gz"     # what goes into boot.img (boot/mkboot.sh)
	(cd "$DIST" && sha256sum Image Image.gz Image.lz4 vmlinux.symvers config | tee SHA256SUMS)
}

steps=("$@"); [ ${#steps[@]} -gt 0 ] || steps=(fetch config build verify dist)
for s in "${steps[@]}"; do
	case $s in fetch|config|build|verify|dist) echo "=== $s"; step_$s ;; *) echo "unknown step $s" >&2; exit 2 ;; esac
done
