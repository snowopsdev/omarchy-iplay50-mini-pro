#!/usr/bin/env python3
"""Regression cases for honest telemetry on the vendor gauge."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[1] / 'tools/tablet-power-status.py'
spec = importlib.util.spec_from_file_location('power_status', MODULE)
power = importlib.util.module_from_spec(spec)
spec.loader.exec_module(power)


class PowerStatus(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)

    def write(self, name, value):
        p = self.root / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(str(value) + '\n')

    def battery(self, **values):
        for name, value in values.items():
            self.write('class/power_supply/battery/' + name, value)

    def test_fallback_soc_is_never_claimed_calibrated(self):
        self.battery(capacity=99, init_done=1, disable=0, charge_full=2946000,
                     charge_full_design=294000, voltage_now=4335000,
                     current_now=3000, temp=290)
        self.write('class/power_supply/mtk-gauge/info_is_nvram_fail_mode', 1)
        self.write('class/power_supply/5-006a/online', 1)
        out = power.status(self.root)
        self.assertEqual(out['battery']['percentage_confidence'], 'fallback_estimate')
        self.assertFalse(out['battery']['charge_design_consistent'])
        self.assertTrue(out['external_power'])
        self.assertEqual(out['battery']['temperature_c'], 29)
        self.assertEqual(out['battery']['voltage_v'], 4.335)
        self.assertIsNone(out['battery']['runtime_estimate'])

    def test_uninitialized_and_missing_are_unavailable(self):
        self.assertEqual(power.status(self.root)['battery']['percentage_confidence'], 'unavailable')
        self.battery(capacity=-1, init_done=0, disable=0)
        self.assertIsNone(power.status(self.root)['battery']['percentage'])
        self.battery(capacity=50, init_done=1, disable=1)
        self.assertIsNone(power.status(self.root)['battery']['percentage'])

    def test_valid_register_flag_still_does_not_validate_calibration(self):
        self.battery(capacity=42, init_done=1, disable=0)
        self.write('class/power_supply/mtk-gauge/info_is_nvram_fail_mode', 0)
        self.assertEqual(power.status(self.root)['battery']['percentage_confidence'], 'calibration_unverified')

    def test_vendor_temperature_sentinel_is_not_shown_as_real(self):
        self.write('class/thermal/thermal_zone0/type', 'mtk-master-charger')
        self.write('class/thermal/thermal_zone0/temp', -127000)
        out = power.status(self.root)
        self.assertIsNone(out['thermal'][0]['temperature_c'])
        self.assertFalse(out['thermal'][0]['available'])

    def test_wakeup_name_with_spaces_and_indefinite_active_lock(self):
        self.write('kernel/debug/wakeup_sources',
                   'name\tactive_count\tevent_count\twakeup_count\texpire_count\tactive_since\ttotal_time\tmax_time\tlast_change\tprevent_suspend_time\n'
                   'USB suspend lock\t1\t1\t0\t0\t5000\t5000\t5000\t100\t0\n'
                   'battery\t1\t1\t0\t0\t0\t10\t10\t100\t0')
        self.write('power/state', 'freeze mem')
        self.write('power/mem_sleep', '[s2idle]')
        out = power.status(self.root)
        self.assertEqual(out['sleep']['active_wake_sources'], [{'name': 'USB suspend lock', 'active_ms': 5000}])
        self.assertFalse(out['sleep']['automatic_suspend_ready'])


if __name__ == '__main__':
    unittest.main()
