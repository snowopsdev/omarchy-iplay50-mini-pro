#!/bin/bash
# Public-repo adaptation: SSH via tools/tablet (the -N -L tunnel relies on OpenSSH accepting options
# after the destination, which tools/tablet passes through); stages ctl.sh/page.html to /tmp/bench.
# Not re-run against the device in this form.
# Auto DDR scaling vs DDR forced to its top in-spec OPP (4266 MHz).
set -u
P=$(cd "$(dirname "$0")/../.." && pwd); T="$P/tools/tablet"
tar -C "$P/perf/chromium-bench" -cf - ctl.sh page.html | "$T" 'install -d /tmp/bench && tar -C /tmp/bench --no-same-owner -xf -'
F=/sys/kernel/helio-dvfsrc/dvfsrc_force_vcore_dvfs_opp
"$T" -N -L 19333:127.0.0.1:9333 2>/dev/null & tun=$!
trap 'kill $tun 2>/dev/null; "$T" "bash /tmp/bench/ctl.sh stop >/dev/null 2>&1; echo 255 > $F; grep DDR /sys/kernel/helio-dvfsrc/dvfsrc_dump | head -1"' EXIT
sleep 2
for opp in 255 0 255 0; do
  "$T" "echo $opp > $F; sleep 0.5; bash /tmp/bench/ctl.sh start finish >/dev/null 2>&1"
  echo "== force_opp=$opp $("$T" "grep 'DDR ' /sys/kernel/helio-dvfsrc/dvfsrc_dump | head -1" | tr -s ' ')"
  BENCH_PAGE=file:///tmp/bench/page.html timeout 60 node "$P/perf/chromium-bench/bench.mjs" "opp$opp" 2>&1 | grep label
  timeout 200 node "$P/perf/chromium-bench/sites.mjs" https://www.youtube.com/ https://en.wikipedia.org/wiki/Linux 2>&1 | grep site
  "$T" "bash /tmp/bench/ctl.sh stop >/dev/null 2>&1"
done
