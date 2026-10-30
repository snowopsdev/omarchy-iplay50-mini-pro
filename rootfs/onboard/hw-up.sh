#!/bin/sh
# hw-up.sh: bring up the tablet's vendor hardware at boot (run by omarchy-hw.service).
# Order matters: Wi-Fi modules must load only after mtk-wmt-init's loader stage (EMI base is latched at insmod).
# In power-off-charging mode (lk boots us with androidboot.mode=charger) only power/gauge hardware comes up.
D=/opt/omarchy-tablet
cd $D
mkdir -p /run/omarchy-hw
# Keep the tablet receiving power when connected; OTG can be requested manually.
[ -w /sys/class/typec/port0/preferred_role ] && echo sink > /sys/class/typec/port0/preferred_role
BL=/sys/class/leds/lcd-backlight
setbl() { [ -w $BL/brightness ] && echo $(( $(cat $BL/max_brightness) * $1 / 100 )) > $BL/brightness; }
./mount-vendor.sh
if grep -q 'androidboot.mode = "charger"' /proc/bootconfig 2>/dev/null; then
	: > /run/omarchy-hw/charger-mode
	echo "hw-up: charger mode: power/gauge only" > /dev/kmsg
	./load-modules.sh load-power.list load-extra.list
	setbl 5
	# Gauge initialization belongs to fgd-kernel-mode.service, enabled after validation.
	exit 0
fi
setbl 40
./load-modules.sh load-power.list load-gpu.list load-audio.list load-extra.list load-conn-nowifi.list
./load-modules.sh load-sensors.list
mkdir -p /run/mtk-wmt
BIN=$D/mtk-wmt-init MODDIR=/vendor_dlkm/lib/modules sh ./wifi-up.sh > /run/mtk-wmt/wifi-up.out 2>&1 || echo "hw-up: wifi-up failed ($?)" > /dev/kmsg
if [ -w /proc/net/wlan/dbgLevel ]; then
	i=0
	while [ "$i" -lt 32 ]; do
		printf '0x%02x:0x03\n' "$i" > /proc/net/wlan/dbgLevel
		i=$((i + 1))
	done
fi
BIN=$D/mtk-bt-bridge sh ./bt-up.sh > /run/omarchy-hw/bt-up.out 2>&1 || echo "hw-up: bt-up failed ($?)" > /dev/kmsg
echo "hw-up: done: $(grep -c '^ok' /run/omarchy-hw/load.log) modules, wlan0=$([ -e /sys/class/net/wlan0 ] && echo yes || echo no), hci0=$([ -e /sys/class/bluetooth/hci0 ] && echo yes || echo no)" > /dev/kmsg
