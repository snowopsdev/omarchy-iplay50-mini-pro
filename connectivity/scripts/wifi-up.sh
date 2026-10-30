#!/omarchy/bin/busybox sh
# wifi-up.sh - bring up MT6789 connsys Wi-Fi from plain Linux userspace (replaces Android wmt_loader + wmt_launcher + wlan_assistant).
#
# Run on the tablet as root, with the vendor modules already loaded (wmt_drv, wmt_chrdev_wifi, wlan_drv_gen4m_6789, btif_drv, connadp,
# connfem, cfg80211) and /vendor mounted (vendor mount step, see docs/05-wifi-bluetooth.md).  Works with busybox sh; `sh wifi-up.sh` also works with any POSIX sh.
#
#   1. checks prerequisites (/dev/wmtdetect, modules, /vendor/firmware/WMT_SOC.cfg)
#   2. mounts the nvdata partition READ-ONLY (ro,noload: no journal replay, nothing is ever written) at /mnt/vendor/nvdata
#   3. runs mtk-wmt-init (loader + launcher daemon; powers connsys and downloads the ROM-patch firmware)
#   4. pushes the factory Wi-Fi NVRAM (MAC/calibration) to the driver if the file exists (optional; driver works without it)
#   5. echo MODE > /dev/wmtWifi   (1 = Wi-Fi function on -> wlan0;  P or S = on + p2p0;  A = AP mode)
#   6. waits for wlan0 and prints diagnostics (dmesg tail, ip link)
#
# IMPORTANT (found on hardware): wlan_drv_gen4m_6789 copies wmt_drv's EMI base (gConEmiPhyBase) into gConEmiPhyBaseFinal ONCE in its module_init, and wmt_drv only
# fills that variable when the loader's SOC_HW_INIT ioctl runs.  So gen4m (and wmt_chrdev_wifi) must be insmod'ed AFTER mtk-wmt-init's loader stage.  If they are
# already loaded when this script starts and the loader has not run yet, they are rmmod'ed and insmod'ed again (RELOAD=auto); RELOAD=1 forces, RELOAD=0 never.
# env knobs: MODDIR=/vendor_dlkm/lib/modules (where wmt_chrdev_wifi.ko and wlan_drv_gen4m_6789.ko live)  RELOAD=auto|1|0
#            (older knobs:) MODE=1|P|S|A (default 1)  UP=1 (also `ip link set wlan0 up`)  NVDATA=0 (skip nvdata/NVRAM)  NVDEV=/dev/sdc6 (override partition)
#            BIN=/path/to/mtk-wmt-init  PREP=/path/to/prep.sh (run if /vendor/firmware is missing)  WAIT=40 (seconds to wait for wlan0)
#            WMT_ARGS="--no-poweron" (extra args for mtk-wmt-init)  NOINIT=1 (skip step 3, daemon already running)
# NOTE: powering the connsys has been seen to glitch the USB gadget link on other MediaTek ports; if your shell is the NCM/telnet link, run detached:
#         setsid sh wifi-up.sh > /run/mtk-wmt/wifi-up.out 2>&1 &    and read /run/mtk-wmt/wifi-up.out afterwards.
# exit codes: 0 wlan0 present, 1 prerequisites, 2 mtk-wmt-init failed, 3 Wi-Fi power-on write failed, 4 wlan0 never appeared
PATH=/omarchy/bin:/usr/sbin:/usr/bin:/sbin:/bin:$PATH; export PATH
HERE=$(dirname "$0")
BIN=${BIN:-$HERE/mtk-wmt-init}
MODE=${MODE:-1}
WAIT=${WAIT:-40}
ST=/run/mtk-wmt
NVMNT=/mnt/vendor/nvdata
FW=/vendor/firmware
WLOG=$ST/wifi-up.log
mkdir -p $ST 2>/dev/null

say() { echo "wifi-up: $*"; echo "<6>wifi-up: $*" > /dev/kmsg 2>/dev/null; echo "$(cut -d' ' -f1 /proc/uptime) $*" >> $WLOG 2>/dev/null; }
die() { say "ERROR: $*"; diag; exit "${2:-1}"; }

diag() {
	echo "================ diagnostics ================"
	echo "---- state ($ST)"; for f in $ST/*; do case "$f" in *.log) ;; *) [ -f "$f" ] && echo "$(basename $f)=$(cat $f)";; esac; done 2>/dev/null
	echo "---- mtk-wmt-init log (tail)"; tail -n 25 $ST/mtk-wmt-init.log 2>/dev/null
	echo "---- dmesg: connectivity lines (last 80)"
	dmesg 2>/dev/null | grep -E 'WMT|wmt|MTK-WIFI|STP|BTIF|CONSYS|connsys|Rom Patch|rom patch|wlan|WLAN|mtk-wmt-init|firmware|MPU|NVRAM|Nvram' | tail -n 80
	echo "---- dmesg tail (40)"; dmesg 2>/dev/null | tail -n 40
	echo "---- ip link"; ip link 2>/dev/null || ifconfig -a 2>/dev/null || ls /sys/class/net
	[ -x "$(command -v iw)" ] && { echo "---- iw dev"; iw dev; }
	echo "============================================="
}

# ---- 1. prerequisites
[ "$(id -u)" = 0 ] || die "must run as root"
[ -x "$BIN" ] || die "mtk-wmt-init not found/executable at $BIN (set BIN=)"
[ -c /dev/wmtdetect ] || die "/dev/wmtdetect missing: wmt_drv.ko not loaded?"
MODDIR=${MODDIR:-/vendor_dlkm/lib/modules}
RELOAD=${RELOAD:-auto}
for m in wmt_drv cfg80211; do
	grep -q "^$m " /proc/modules || die "kernel module $m is not loaded (load the connectivity module list first)"
done
# was gen4m loaded before the loader stage?  (state file driver.ready is written by the loader stage of mtk-wmt-init)
PRELOADED=0; grep -q '^wlan_drv_gen4m_6789 ' /proc/modules && PRELOADED=1
LOADER_DONE=0; [ -f $ST/driver.ready ] && LOADER_DONE=1
NEED_RELOAD=0
case "$RELOAD" in
	1) NEED_RELOAD=1 ;;
	auto) [ $PRELOADED = 1 ] && [ $LOADER_DONE = 0 ] && NEED_RELOAD=1 ;;
esac
say "gen4m preloaded=$PRELOADED loader_done=$LOADER_DONE -> reload=$NEED_RELOAD"
if [ $NEED_RELOAD = 1 ] || [ $PRELOADED = 0 ]; then
	[ -f $MODDIR/wlan_drv_gen4m_6789.ko ] && [ -f $MODDIR/wmt_chrdev_wifi.ko ] || die "cannot find wlan_drv_gen4m_6789.ko / wmt_chrdev_wifi.ko in $MODDIR (mount vendor_dlkm first, or set MODDIR)"
fi
if [ ! -f $FW/WMT_SOC.cfg ]; then
	[ -n "$PREP" ] && { say "running $PREP to mount /vendor"; sh "$PREP"; }
	[ -f $FW/WMT_SOC.cfg ] || die "$FW/WMT_SOC.cfg missing: mount /vendor first (vendor mount step, see docs/05-wifi-bluetooth.md); the kernel reads WMT_SOC.cfg and the ROM-patch firmware via request_firmware during bring-up"
fi
ls $FW | grep -q '^soc2_2_ram_wifi_.*_hdr.bin$' || say "WARNING: no soc2_2_ram_wifi_*_hdr.bin in $FW"
say "dmesg line count before: $(dmesg | wc -l)"

# ---- 2. nvdata read-only
NVOK=0
if [ "${NVDATA:-1}" != 0 ]; then
	if grep -q " $NVMNT " /proc/mounts; then
		NVOK=1
	else
		DEV=$NVDEV
		if [ -z "$DEV" ]; then
			for d in /sys/class/block/*; do
				grep -q '^PARTNAME=nvdata$' $d/uevent 2>/dev/null && { DEV=/dev/$(basename $d); break; }
			done
		fi
		if [ -n "$DEV" ] && [ -b "$DEV" ]; then
			mkdir -p $NVMNT
			blockdev --setro "$DEV" 2>/dev/null          # belt and braces: block layer refuses writes
			if mount -t ext4 -o ro,noload,nodev,nosuid,noexec "$DEV" $NVMNT 2>/dev/null; then
				NVOK=1; echo "$DEV" > $ST/nvdata.mounted-by-wifi-up; say "nvdata $DEV mounted read-only at $NVMNT"
			else
				say "WARNING: could not mount $DEV read-only at $NVMNT (continuing without NVRAM)"
			fi
		else
			say "WARNING: nvdata partition not found (PARTNAME=nvdata); continuing without NVRAM"
		fi
	fi
fi

# ---- 3. loader + launcher daemon
if [ "${NOINIT:-0}" != 1 ]; then
	say "starting $BIN $WMT_ARGS"
	"$BIN" $WMT_ARGS
	rc=$?
	[ $rc = 0 ] || die "mtk-wmt-init failed (rc=$rc)" 2
	say "mtk-wmt-init OK"
fi
[ -c /dev/stpwmt ] || die "/dev/stpwmt still missing after mtk-wmt-init" 2

# ---- 3b. (re)load the Wi-Fi modules now that connsys EMI info exists (see header)
if [ $NEED_RELOAD = 1 ] || [ $PRELOADED = 0 ] || [ -f $ST/wlan.stale ]; then
	if grep -q '^wlan_drv_gen4m_6789 ' /proc/modules; then
		say "unloading stale wlan_drv_gen4m_6789 / wmt_chrdev_wifi"
		[ -d /sys/class/net/wlan0 ] && ip link set wlan0 down 2>/dev/null
		[ -c /dev/wmtWifi ] && printf 0 > /dev/wmtWifi 2>/dev/null
		rmmod wlan_drv_gen4m_6789 || die "rmmod wlan_drv_gen4m_6789 failed (in use? reboot and let wifi-up.sh load it)" 1
		grep -q '^wmt_chrdev_wifi ' /proc/modules && { rmmod wmt_chrdev_wifi || die "rmmod wmt_chrdev_wifi failed" 1; }
	fi
	rm -f $ST/wlan.stale
	grep -q '^wmt_chrdev_wifi ' /proc/modules || insmod $MODDIR/wmt_chrdev_wifi.ko || die "insmod wmt_chrdev_wifi failed" 1
	insmod $MODDIR/wlan_drv_gen4m_6789.ko || die "insmod wlan_drv_gen4m_6789 failed" 1
	say "wmt_chrdev_wifi + wlan_drv_gen4m_6789 loaded after the loader stage"
	# make sure /dev/wmtWifi exists (no devtmpfs/mdev hotplug => mknod from sysfs)
	i=0
	while [ ! -c /dev/wmtWifi ] && [ $i -lt 10 ]; do
		for d in /sys/class/*/wmtWifi/dev; do
			[ -f "$d" ] && { mm=$(cat "$d"); mknod /dev/wmtWifi c ${mm%:*} ${mm#*:}; break; }
		done
		[ -c /dev/wmtWifi ] || sleep 1; i=$((i+1))
	done
fi
[ -c /dev/wmtWifi ] || die "/dev/wmtWifi missing" 1

# ---- 4. NVRAM (optional)
NVF=$NVMNT/APCFG/APRDEB/WIFI
if [ "$NVOK" = 1 ] && [ -f $NVF ]; then
	"$BIN" wifi-nvram --file $NVF && say "Wi-Fi NVRAM pushed" || say "WARNING: NVRAM push failed; driver will use defaults/random MAC"
else
	say "no NVRAM file ($NVF): driver will use defaults (random MAC, default country)"
fi

# ---- 5. Wi-Fi on
say "Wi-Fi function on: echo $MODE > /dev/wmtWifi (blocks until firmware is up)"
if ! printf '%s' "$MODE" > /dev/wmtWifi; then
	die "write to /dev/wmtWifi failed (see 'WMT turn on WIFI fail' lines above)" 3
fi

# ---- 6. wait for wlan0
i=0
while [ $i -lt $WAIT ]; do
	[ -d /sys/class/net/wlan0 ] && break
	sleep 1; i=$((i+1))
done
if [ ! -d /sys/class/net/wlan0 ]; then
	die "wlan0 did not appear within ${WAIT}s" 4
fi
[ "${UP:-0}" = 1 ] && ip link set wlan0 up
say "wlan0 present after ${i}s: $(cat /sys/class/net/wlan0/address 2>/dev/null)"
diag
exit 0
