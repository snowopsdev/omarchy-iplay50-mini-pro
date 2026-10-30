#!/usr/bin/env python3
"""Read-only MediaTek tablet power telemetry; never calibrates or suspends.

Uses a strict attribute allowlist: enumerating every vendor sysfs attribute can
invoke diagnostic getters with hardware side effects. JSON is suitable for UI
status; retain the confidence fields when displaying the percentage.
"""
import argparse
import json
from pathlib import Path


def read(path):
    try:
        return path.read_text().strip()
    except (OSError, UnicodeError):
        return None


def number(path):
    value = read(path)
    try:
        return int(value) if value is not None else None
    except ValueError:
        return None


def active_wake_sources(path):
    """debugfs columns are tab-delimited; wake-source names contain spaces."""
    data = read(path)
    if data is None:
        return None
    lines = data.splitlines()
    if not lines:
        return []
    columns = [v.strip() for v in lines[0].split('\t') if v.strip()]
    if 'active_since' not in columns:
        return None
    result = []
    for line in lines[1:]:
        values = [v.strip() for v in line.split('\t') if v.strip()]
        if len(values) != len(columns):
            continue
        row = dict(zip(columns, values))
        try:
            active_ms = int(row['active_since'])
        except ValueError:
            continue
        if active_ms:
            result.append({'name': row['name'], 'active_ms': active_ms})
    return result


def status(sysroot):
    psy = sysroot / 'class/power_supply'
    battery = psy / 'battery'
    gauge = psy / 'mtk-gauge'
    result = {'schema_version': 1, 'read_only': True, 'warnings': []}
    capacity = number(battery / 'capacity')
    initialized = number(battery / 'init_done')
    disabled = number(battery / 'disable')
    nvram_fail = number(gauge / 'info_is_nvram_fail_mode')
    confidence = 'unverified'
    if initialized != 1 or disabled == 1 or capacity is None or not 0 <= capacity <= 100:
        confidence = 'unavailable'
    elif nvram_fail == 1:
        confidence = 'fallback_estimate'
        result['warnings'].append('Battery percentage is a fallback estimate; capacity calibration is unverified.')
    elif nvram_fail == 0:
        confidence = 'calibration_unverified'
    charge_full = number(battery / 'charge_full')
    charge_design = number(battery / 'charge_full_design')
    design_consistent = (None if not charge_full or not charge_design else
                         0.5 <= charge_design / charge_full <= 2)
    if design_consistent is False:
        result['warnings'].append('Reported full and design charge disagree; do not derive battery health or runtime from them.')
    voltage = number(battery / 'voltage_now')
    current = number(battery / 'current_now')
    temp = number(battery / 'temp')
    result['battery'] = {
        'present': number(battery / 'present'),
        'status': read(battery / 'status'),
        'health': read(battery / 'health'),
        'percentage': capacity if confidence != 'unavailable' else None,
        'percentage_confidence': confidence,
        'gauge_initialized': initialized,
        'nvram_fail_mode': nvram_fail,
        'voltage_v': voltage / 1e6 if voltage is not None else None,
        'current_a': current / 1e6 if current is not None else None,
        'temperature_c': temp / 10 if temp is not None else None,
        'charge_full_uah': charge_full,
        'charge_full_design_uah': charge_design,
        'charge_design_consistent': design_consistent,
        # Vendor driver synthesizes charge_counter from ui_soc and qmax. It is
        # deliberately excluded, along with misleading time-to-full estimates.
        'runtime_estimate': None,
    }
    chargers = []
    if psy.exists():
        for path in sorted(psy.iterdir()):
            if path.name in ('battery', 'mtk-gauge'):
                continue
            online = number(path / 'online')
            if online is None:
                continue
            chargers.append({'name': path.name, 'online': online == 1,
                             'status': read(path / 'status'), 'type': read(path / 'type')})
    result['chargers'] = chargers
    result['external_power'] = any(p['online'] for p in chargers)
    typec = sysroot / 'class/typec/port0'
    result['usb'] = {a: read(typec / a) for a in ('data_role', 'power_role', 'preferred_role')}
    thermal = []
    for path in sorted((sysroot / 'class/thermal').glob('thermal_zone*')):
        value = number(path / 'temp')
        # -127 C is a vendor "unavailable" sentinel, not a cold component.
        valid = value is not None and -40000 <= value <= 150000
        thermal.append({'name': read(path / 'type'),
                        'temperature_c': value / 1000 if valid else None,
                        'available': valid})
    result['thermal'] = thermal
    states = read(sysroot / 'power/state')
    mem_sleep = read(sysroot / 'power/mem_sleep')
    wake = active_wake_sources(sysroot / 'kernel/debug/wakeup_sources')
    rtc = []
    for path in sorted((sysroot / 'class/rtc').glob('rtc*')):
        rtc.append({'name': read(path / 'name'), 'wake_enabled': read(path / 'device/power/wakeup') == 'enabled',
                    'alarm': read(path / 'wakealarm')})
    result['sleep'] = {'supported_states': states.split() if states else [],
                       'mem_sleep': mem_sleep, 'active_wake_sources': wake,
                       'rtc': rtc, 'validated': False,
                       'automatic_suspend_ready': False}
    if wake:
        result['warnings'].append('Kernel wake sources remain active; suspend needs a coordinated display/USB/audio test.')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sysroot', type=Path, default=Path('/sys'), help='sysfs root (override for offline fixtures)')
    args = parser.parse_args()
    print(json.dumps(status(args.sysroot), indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
