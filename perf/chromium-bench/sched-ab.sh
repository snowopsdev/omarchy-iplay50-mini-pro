#!/bin/bash
# Public-repo adaptation: SSH via tools/tablet (the -N -L tunnel relies on OpenSSH accepting options
# after the destination, which tools/tablet passes through); stages ctl.sh/page.html to /tmp/bench.
# Not re-run against the device in this form.
# Compare scheduler settings on real-site loads in the bench browser.
set -u
P=$(cd "$(dirname "$0")/../.." && pwd); T="$P/tools/tablet"
tar -C "$P/perf/chromium-bench" -cf - ctl.sh page.html | "$T" 'install -d /tmp/bench && tar -C /tmp/bench --no-same-owner -xf -'
"$T" -N -L 19333:127.0.0.1:9333 & tun=$!
orig=$("$T" 'echo $(cat /proc/sys/kernel/sched_energy_aware) $(cat /proc/sys/kernel/sched_pelt_multiplier)')
trap 'kill $tun 2>/dev/null; "$T" "bash /tmp/bench/ctl.sh stop >/dev/null 2>&1; set -- $orig; echo \$1 > /proc/sys/kernel/sched_energy_aware; echo \$2 > /proc/sys/kernel/sched_pelt_multiplier"' EXIT
sleep 2
for cfg in "1 1" "1 4" "0 4" "1 1"; do
  set -- $cfg
  "$T" "echo $1 > /proc/sys/kernel/sched_energy_aware; echo $2 > /proc/sys/kernel/sched_pelt_multiplier; bash /tmp/bench/ctl.sh start finish >/dev/null 2>&1"
  echo "== eas=$1 pelt=$2"
  timeout 200 node "$P/perf/chromium-bench/sites.mjs" ${SITES:-https://www.youtube.com/ https://en.wikipedia.org/wiki/Linux} 2>&1 | grep site
  "$T" "bash /tmp/bench/ctl.sh stop >/dev/null 2>&1"
done
