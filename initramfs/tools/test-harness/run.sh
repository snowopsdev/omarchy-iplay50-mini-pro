#!/omarchy/bin/busybox sh
# usage: run.sh <seconds> ; env passes through
export PATH=/omarchy/bin
: > /dev/kmsg
/linuxinit &
sleep $1
echo "===== KMSG (non-module lines)"; grep -v "insmod .* ok$\|omarchy-stage0: insmod [^ ]*$" /dev/kmsg | cut -c1-210
echo "===== insmod trace: $(wc -l < /run/insmod.trace) modules; first: $(head -3 /run/insmod.trace | tr '\n' ' ')"
echo "===== reboot trace"; cat /run/reboot.trace
echo "===== markers: $(ls /run/omarchy | tr '\n' ' ')"
echo "===== full kmsg (module lines excluded) saved"; grep -v "insmod" /dev/kmsg > /run/kmsg.short; kill -9 -1
