#!/usr/bin/python3
"""Analyse tablet-battery-log CSVs for the calibration cycle.

usage: battery-cycle-report.py LOGDIR
For each unplugged stretch (charger offline), integrate battery current over
time (trapezoid; gaps over 5 min or a reboot end a stretch) and compare the
charge drawn with the reported percentage drop:
    estimated usable capacity = mAh drawn / (percent dropped / 100)
Also prints the voltage at each 10% step, which shows whether the gauge's
percentage tracks voltage sensibly. Read-only analysis of local files.
"""
import csv, sys
from pathlib import Path

rows = []
for p in sorted(Path(sys.argv[1]).glob('*.csv')):
    with open(p) as f:
        for r in csv.DictReader(f):
            try:
                rows.append(dict(t=int(r['epoch']), boot=r['boot_id'], pct=int(r['capacity']),
                                 mv=int(r['voltage_uv']) / 1000, ma=int(r['current_ua']) / 1000,
                                 online=r['charger_online'] == '1', status=r['status']))
            except (ValueError, KeyError):
                pass
rows.sort(key=lambda r: r['t'])
if not rows:
    sys.exit('no samples')
print(f"{len(rows)} samples, {rows[0]['t']} .. {rows[-1]['t']} "
      f"({(rows[-1]['t'] - rows[0]['t']) / 3600:.1f} h)")

segments, cur = [], []
for r in rows:
    if cur and (r['online'] or r['boot'] != cur[-1]['boot'] or r['t'] - cur[-1]['t'] > 300):
        segments.append(cur); cur = []
    if not r['online']:
        cur.append(r)
if cur:
    segments.append(cur)

total_mah = total_pct = 0
for seg in segments:
    if len(seg) < 10:
        continue
    mah = 0.0
    for a, b in zip(seg, seg[1:]):
        mah += -(a['ma'] + b['ma']) / 2 * (b['t'] - a['t']) / 3600
    dpct = seg[0]['pct'] - seg[-1]['pct']
    hours = (seg[-1]['t'] - seg[0]['t']) / 3600
    est = mah / (dpct / 100) if dpct > 0 else float('nan')
    print(f"unplugged {hours:.1f} h: {seg[0]['pct']}% -> {seg[-1]['pct']}% "
          f"({seg[0]['mv']:.0f} -> {seg[-1]['mv']:.0f} mV), drew {mah:.0f} mAh, "
          f"avg {mah / hours if hours else 0:.0f} mA, implied capacity {est:.0f} mAh")
    steps = {}
    for r in seg:
        steps.setdefault(r['pct'] // 10 * 10, r['mv'])
    print('   voltage at % step:', ', '.join(f"{k}%={v:.0f}mV" for k, v in sorted(steps.items(), reverse=True)))
    if dpct > 0:
        total_mah += mah; total_pct += dpct
if total_pct >= 30:
    print(f"OVERALL implied usable capacity: {total_mah / (total_pct / 100):.0f} mAh "
          f"(gauge assumes 2946 mAh) over {total_pct}% of discharge")
else:
    print(f"Not enough unplugged discharge yet for a capacity estimate "
          f"({total_pct}% so far; need at least 30%, ideally 100% -> ~15%).")
