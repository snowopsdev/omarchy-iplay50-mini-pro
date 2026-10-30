#!/bin/sh
# load-modules.sh LIST...: insmod vendor_dlkm modules in list order, skipping loaded ones; logs to /run/omarchy-hw/load.log
M=/vendor_dlkm/lib/modules; mkdir -p /run/omarchy-hw; LOG=/run/omarchy-hw/load.log
for list in "$@"; do
	echo "=== $(cut -d' ' -f1 /proc/uptime) $list" >> $LOG
	while read -r f; do
		[ -z "$f" ] && continue; m=${f%.ko}
		grep -q "^$(echo $m | tr - _) " /proc/modules && continue
		if r=$(insmod $M/$f 2>&1); then echo "ok   $m" >> $LOG; else echo "FAIL $m: $r" >> $LOG; fi
	done < "$list"
done
