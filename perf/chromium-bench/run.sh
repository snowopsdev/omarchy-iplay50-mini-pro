#!/bin/bash
# Public-repo adaptation: SSH via tools/tablet (the -N -L tunnel relies on OpenSSH accepting options
# after the destination, which tools/tablet passes through); stages ctl.sh/page.html to /tmp/bench.
# Not re-run against the device in this form.
# Desktop driver: alternate flush/finish benchmark browsers, 2 runs each.
set -u
P=$(cd "$(dirname "$0")/../.." && pwd)
T="$P/tools/tablet"
tar -C "$P/perf/chromium-bench" -cf - ctl.sh page.html | "$T" 'install -d /tmp/bench && tar -C /tmp/bench --no-same-owner -xf -'
"$T" -N -L 19333:127.0.0.1:9333 & tunnel=$!
trap 'kill $tunnel 2>/dev/null; "$T" "bash /tmp/bench/ctl.sh stop"' EXIT
sleep 2
for mode in ${MODES:-flush finish flush finish}; do
  "$T" "bash /tmp/bench/ctl.sh start $mode"
  for i in 1 2; do
    BENCH_PAGE=file:///tmp/bench/page.html node "$P/perf/chromium-bench/bench.mjs" "$mode" || echo "{\"label\":\"$mode\",\"error\":true}"
  done
  "$T" "bash /tmp/bench/ctl.sh stop"
done
