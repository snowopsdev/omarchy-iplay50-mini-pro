#!/bin/bash
# Public-repo adaptation: SSH via tools/tablet (the -N -L tunnel relies on OpenSSH accepting options
# after the destination, which tools/tablet passes through); stages ctl.sh/page.html to /tmp/bench.
# Not re-run against the device in this form.
# Current GL (llvmpipe) path vs --disable-gpu software compositing.
set -u
P=$(cd "$(dirname "$0")/../.." && pwd); T="$P/tools/tablet"
tar -C "$P/perf/chromium-bench" -cf - ctl.sh page.html | "$T" 'install -d /tmp/bench && tar -C /tmp/bench --no-same-owner -xf -'
"$T" -N -L 19333:127.0.0.1:9333 2>/dev/null & tun=$!
trap 'kill $tun 2>/dev/null; "$T" "bash /tmp/bench/ctl.sh stop >/dev/null 2>&1"' EXIT
sleep 2
for v in gl nogpu gl nogpu; do
  "$T" "bash /tmp/bench/ctl.sh start finish $v >/dev/null 2>&1"
  echo "== $v"
  BENCH_PAGE=file:///tmp/bench/page.html timeout 60 node "$P/perf/chromium-bench/bench.mjs" "$v" 2>&1 | grep label
  timeout 200 node "$P/perf/chromium-bench/sites.mjs" https://www.youtube.com/ https://en.wikipedia.org/wiki/Linux 2>&1 | grep site
  "$T" "bash /tmp/bench/ctl.sh stop >/dev/null 2>&1"
done
