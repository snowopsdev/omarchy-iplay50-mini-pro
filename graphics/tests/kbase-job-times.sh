#!/bin/sh
# Mali job times per job slot (0 = fragment, 1 = vertex/tiler/compute), from
# kprobes on the vendor kbase module: kbase_job_hw_submit -> kbase_job_done.
# Root. Usage: kbase-job-times.sh [DELAY_S] [WINDOW_S]; start the workload,
# the trace window opens after DELAY_S. Needs the module named below.
set -eu
mod=${KBASE_MODULE:-mali_kbase_mt6789}
cd /sys/kernel/tracing
echo 0 > tracing_on; echo > trace; echo > kprobe_events
echo "p:k_submit $mod:kbase_job_hw_submit katom=%x1:x64 js=%x2:u32" >> kprobe_events
echo "p:k_done $mod:kbase_job_done done=%x1:x32" >> kprobe_events
echo 1 > events/kprobes/enable
sleep "${1:-2}"; echo 1 > tracing_on; sleep "${2:-1}"; echo 0 > tracing_on
echo 0 > events/kprobes/enable
out=$(mktemp); grep -v "^#" trace > "$out"; echo > kprobe_events
python3 - "$out" <<"PY"
import collections, re, sys
last = {}; d = collections.defaultdict(list)
for l in open(sys.argv[1]):
    m = re.search(r" (\d+\.\d+): k_(submit|done):.*?(?:js|done)=(\S+)", l)
    if not m: continue
    t = float(m.group(1))
    if m.group(2) == "submit": last[int(m.group(3))] = t
    else:
        mask = int(m.group(3), 16)
        for js in range(3):
            if mask & (1 << js) and js in last: d[js].append((t - last.pop(js)) * 1e6)
for js, v in sorted(d.items()):
    v.sort()
    print("slot", js, "jobs", len(v), "busy ms %.1f" % (sum(v) / 1000), "median us", round(v[len(v) // 2]),
          "p90", round(v[int(len(v) * .9)]), "max", round(v[-1]))
PY
rm -f "$out"
