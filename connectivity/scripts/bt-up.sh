#!/omarchy/bin/busybox sh
# bt-up.sh - bring up Bluetooth (UNTESTED on hardware).  Needs wifi-up.sh's mtk-wmt-init daemon (or run it first), modules bt_drv_connac1x +
# btif_drv loaded, /dev/stpbt present.  Starts mtk-bt-bridge in the background; it opens /dev/stpbt (BT function on, firmware already in EMI)
# and attaches the BlueZ H4 line discipline to a pty -> hci0.  Then start bluetoothd (systemd: systemctl start bluetooth).
PATH=/omarchy/bin:/usr/sbin:/usr/bin:/sbin:/bin:$PATH; export PATH
HERE=$(dirname "$0"); ST=/run/mtk-wmt; mkdir -p $ST
INIT=${INIT:-$HERE/mtk-wmt-init}; BR=${BR:-$HERE/mtk-bt-bridge}
say() { echo "bt-up: $*"; echo "<6>bt-up: $*" > /dev/kmsg 2>/dev/null; }
grep -q '^bt_drv_connac1x ' /proc/modules || { say "ERROR: bt_drv_connac1x not loaded"; exit 1; }
[ -f $ST/launcher.ready ] && grep -q yes $ST/launcher.ready || { say "starting mtk-wmt-init"; "$INIT" $WMT_ARGS || { say "ERROR: mtk-wmt-init failed"; exit 2; }; }
[ -c /dev/stpbt ] || { sleep 2; [ -c /dev/stpbt ] || { say "ERROR: /dev/stpbt missing"; exit 1; }; }
BDF=/mnt/vendor/nvdata/APCFG/APRDEB/BT_Addr; ARGS=""
[ -f $BDF ] && ARGS="--bdaddr $BDF"
[ -f $ST/mtk-bt-bridge.pid ] && kill -0 "$(cat $ST/mtk-bt-bridge.pid)" 2>/dev/null && { say "bridge already running"; exit 0; }
"$BR" $ARGS $BT_ARGS >> $ST/mtk-bt-bridge.log 2>&1 &
echo $! > $ST/mtk-bt-bridge.pid
i=0; while [ $i -lt 30 ]; do [ -d /sys/class/bluetooth/hci0 ] && break; sleep 1; i=$((i+1)); done
if [ -d /sys/class/bluetooth/hci0 ]; then say "hci0 present after ${i}s"; else say "ERROR: hci0 never appeared"; tail -n 20 $ST/mtk-bt-bridge.log; dmesg | grep -E 'MTK-BT|Bluetooth|hci|WMT' | tail -n 40; exit 3; fi
dmesg | grep -E 'MTK-BT|Bluetooth|hci0' | tail -n 20
command -v hciconfig >/dev/null && hciconfig -a
exit 0
