#!/usr/bin/python3
"""Read-only battery logger for the capacity calibration cycle.

Every INTERVAL seconds appends one CSV row to /var/lib/tablet-battery-log/
<UTC date>.csv: time, boot id, battery %/voltage/current/status/temperature,
charge counter, charger state and input limit, backlight. Only sysfs reads;
nothing is written to any device. Sign convention observed on this board:
current_now < 0 while the battery supplies the system.
"""
import csv, os, time
from datetime import datetime, timezone
from pathlib import Path

INTERVAL = 30
OUT = Path('/var/lib/tablet-battery-log')
PS = Path('/sys/class/power_supply')
FIELDS = ['utc', 'epoch', 'boot_id', 'capacity', 'voltage_uv', 'current_ua', 'status',
          'temp_dc', 'charge_counter_uah', 'charge_full_uah', 'charger_online',
          'charger_status', 'charger_usb_type', 'input_limit_ua', 'backlight']

def read(path):
    try:
        return Path(path).read_text().strip()
    except OSError:
        return ''

def usb_type(raw):
    for word in raw.split():
        if word.startswith('['):
            return word.strip('[]')
    return raw

boot_id = read('/proc/sys/kernel/random/boot_id')
OUT.mkdir(mode=0o755, parents=True, exist_ok=True)
while True:
    now = datetime.now(timezone.utc)
    b, c = PS / 'battery', PS / '5-006a'
    row = {
        'utc': now.strftime('%Y-%m-%dT%H:%M:%SZ'), 'epoch': int(now.timestamp()), 'boot_id': boot_id,
        'capacity': read(b / 'capacity'), 'voltage_uv': read(b / 'voltage_now'),
        'current_ua': read(b / 'current_now'), 'status': read(b / 'status'),
        'temp_dc': read(b / 'temp'), 'charge_counter_uah': read(b / 'charge_counter'),
        'charge_full_uah': read(b / 'charge_full'), 'charger_online': read(c / 'online'),
        'charger_status': read(c / 'status'), 'charger_usb_type': usb_type(read(c / 'usb_type')),
        'input_limit_ua': read(c / 'input_current_limit'),
        'backlight': read('/sys/class/leds/lcd-backlight/brightness'),
    }
    path = OUT / (now.strftime('%Y%m%d') + '.csv')
    new = not path.exists()
    with open(path, 'a', newline='') as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        if new:
            w.writeheader()
        w.writerow(row)
    time.sleep(INTERVAL - time.time() % INTERVAL)
