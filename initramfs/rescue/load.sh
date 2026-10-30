#!/omarchy/bin/busybox sh
# load.sh <listfile>: insmod modules one at a time from /vendor_dlkm, persisting progress to vendor_boot_b
# (unused slot-b partition) after every step so a hang/watchdog reset still leaves a trace.
. /run/omarchy-parts
M=/vendor_dlkm/lib/modules; LOG=/run/load.log
persist() { dd if=$LOG of=$LOGDEV bs=4096 conv=notrunc,fsync 2>/dev/null; }
echo "=== $(cat /proc/uptime) load $1" >> $LOG; persist
while read -r f; do
	[ -z "$f" ] && continue
	m=${f%.ko}
	grep -q "^$(echo $m | tr - _) " /proc/modules && { echo "skip $m" >> $LOG; continue; }
	echo "try  $m" >> $LOG; persist
	echo "<2>omarchy-load: insmod $m" > /dev/kmsg
	if r=$(insmod $M/$f 2>&1); then echo "ok   $m" >> $LOG; else echo "FAIL $m: $r" >> $LOG; fi
	persist
done < $1
echo "=== done $1" >> $LOG; persist
