#!/omarchy/bin/busybox sh
# wifi-down.sh - turn Wi-Fi off again (counterpart of wifi-up.sh).
#   default : ip link set wlan0 down; echo 0 > /dev/wmtWifi (WMT function off; wlan0 disappears)
#   KILL=1  : additionally stop the mtk-wmt-init daemon (connsys stays powered by the LPBK function until reboot; the daemon is only needed
#             while Wi-Fi/BT are being powered on/off and for chip-reset firmware re-download, so normally leave it running)
#   NVUMOUNT=1 : unmount nvdata if wifi-up.sh mounted it
PATH=/omarchy/bin:/usr/sbin:/usr/bin:/sbin:/bin:$PATH; export PATH
ST=/run/mtk-wmt
say() { echo "wifi-down: $*"; echo "<6>wifi-down: $*" > /dev/kmsg 2>/dev/null; }
[ -d /sys/class/net/wlan0 ] && ip link set wlan0 down 2>/dev/null
if [ -c /dev/wmtWifi ]; then
	printf '0' > /dev/wmtWifi && say "Wi-Fi off" || say "ERROR: write 0 to /dev/wmtWifi failed"
fi
sleep 1
[ -d /sys/class/net/wlan0 ] && say "wlan0 still present" || say "wlan0 gone"
if [ "${KILL:-0}" = 1 ] && [ -f $ST/mtk-wmt-init.pid ]; then
	kill "$(cat $ST/mtk-wmt-init.pid)" 2>/dev/null && say "mtk-wmt-init stopped"
	rm -f $ST/mtk-wmt-init.pid $ST/launcher.ready
fi
if [ "${NVUMOUNT:-0}" = 1 ] && [ -f $ST/nvdata.mounted-by-wifi-up ]; then
	umount /mnt/vendor/nvdata && rm -f $ST/nvdata.mounted-by-wifi-up && say "nvdata unmounted"
fi
dmesg | grep -E 'MTK-WIFI|WMT|wlan' | tail -n 15
ip link 2>/dev/null || true
exit 0
