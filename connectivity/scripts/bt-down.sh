#!/omarchy/bin/busybox sh
# bt-down.sh - stop the Bluetooth bridge (closes /dev/stpbt => BT function off, hci0 removed).
ST=/run/mtk-wmt
[ -f $ST/mtk-bt-bridge.pid ] && kill "$(cat $ST/mtk-bt-bridge.pid)" 2>/dev/null && echo "bt-down: bridge stopped"
rm -f $ST/mtk-bt-bridge.pid
sleep 1; [ -d /sys/class/bluetooth/hci0 ] && echo "bt-down: hci0 still present" || echo "bt-down: hci0 gone"
