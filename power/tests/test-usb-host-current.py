#!/usr/bin/env python3
"""Offline fake-sysfs ownership/transition tests; no target access."""
import importlib.util
import json
import os
import struct
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('host_current', HERE.parent / 'tools/usb-host-current.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
BOOT = '11111111-2222-3333-4444-555555555555'


class FakeKernel(module.Controller):
    def __init__(self, root):
        super().__init__(root, owner_uid=os.getuid())
        self.writes, self.logs = [], []

    def log(self, text):
        self.logs.append(text)

    def write_policy(self, value):
        self.writes.append(value)
        self.policy.write_text(str(value))
        self.target.write_text(str(1200000 if value == -1 else value))


class PolicyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.controller = FakeKernel(self.root)
        self.put('/proc/device-tree/model', b'MT8781V/CA\0')
        self.put('/proc/device-tree/compatible', b'mediatek,MT6789\0')
        self.put('/proc/sys/kernel/random/boot_id', BOOT)
        for name, value in module.BUILD_IDS.items():
            self.put(f'/sys/module/{name}/notes/.note.gnu.build-id',
                     struct.pack('<III', 4, 20, 3) + b'GNU\0' + bytes.fromhex(value))
        self.put('/sys/devices/platform/charger/input_current', '-1')
        self.put('/sys/class/power_supply/mtk-master-charger/input_current_limit', '1200000')
        self.put('/sys/class/power_supply/5-006a/manufacturer', 'SouthChip')
        self.put('/sys/class/power_supply/5-006a/type', 'USB')
        self.put('/sys/class/power_supply/5-006a/online', '1')
        self.usb('SDP')
        (self.root / 'run').mkdir()

    def put(self, path, value):
        destination = self.root / path.lstrip('/')
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(value if isinstance(value, bytes) else value.encode())

    def usb(self, kind):
        self.put('/sys/class/power_supply/5-006a/usb_type', f'Unknown [{kind}] SDP CDP DCP PD')

    def step(self):
        with self.controller.locked():
            self.controller.step()

    def restore(self):
        with self.controller.locked():
            self.controller.restore()

    def test_sdp_apply_idempotent_and_private_ownership(self):
        self.step(); self.step()
        self.assertEqual(self.controller.writes, [500000])
        self.assertEqual(self.controller.state.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.controller.directory.stat().st_mode & 0o777, 0o700)
        self.assertEqual(json.loads(self.controller.state.read_text()),
                         {'version': 1, 'boot_id': BOOT, 'original_policy': -1, 'cap': 500000})

    def test_foreign_positive_zero_and_unowned_500_remain_untouched(self):
        for value in (0, 200000, 500000, 900000):
            with self.subTest(policy=value):
                self.controller.policy.write_text(str(value))
                self.step(); self.restore()
                self.assertEqual(self.controller.writes, [])
                self.assertFalse(self.controller.state.exists())

    def test_unknown_and_offline_hold_our_cap_then_sdp_is_idempotent(self):
        self.step()
        self.usb('Unknown'); self.step()
        self.put('/sys/class/power_supply/5-006a/online', '0'); self.step()
        self.put('/sys/class/power_supply/5-006a/online', '1'); self.usb('SDP'); self.step()
        self.assertEqual(self.controller.writes, [500000])
        self.assertTrue(self.controller.state.exists())

    def test_known_non_sdp_restores_only_our_cap(self):
        for source in ('CDP', 'DCP', 'PD'):
            with self.subTest(source=source):
                self.usb('SDP'); self.step()
                self.usb(source); self.step()
                self.assertEqual(self.controller.writes[-2:], [500000, -1])
                self.assertFalse(self.controller.state.exists())

    def test_changed_foreign_thermal_policy_relinquishes_without_overwrite(self):
        self.step()
        self.controller.policy.write_text('250000')
        self.step(); self.restore(); self.step()
        self.assertEqual(self.controller.writes, [500000])
        self.assertFalse(self.controller.state.exists())
        self.assertEqual(self.controller.policy.read_text(), '250000')

    def test_restart_recognizes_owned_state_and_stop_restores_once(self):
        self.step()
        self.controller = FakeKernel(self.root)
        self.step()
        self.assertEqual(self.controller.writes, [])
        self.restore(); self.restore()
        self.assertEqual(self.controller.writes, [-1])

    def test_stale_boot_record_cannot_restore_or_claim_existing_500(self):
        self.step()
        self.put('/proc/sys/kernel/random/boot_id', 'aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee')
        self.restore(); self.step()
        self.assertEqual(self.controller.writes, [500000])
        self.assertFalse(self.controller.state.exists())

    def test_explicit_adoption_does_not_write_or_momentarily_uncap(self):
        self.controller.policy.write_text('500000')
        with self.controller.locked():
            self.controller.adopt()
        self.step()
        self.assertEqual(self.controller.writes, [])
        self.restore()
        self.assertEqual(self.controller.writes, [-1])

    def test_identity_mismatch_fails_without_policy_write(self):
        note = '/sys/module/mtk_charger_framework/notes/.note.gnu.build-id'
        self.put(note, struct.pack('<III', 4, 20, 3) + b'GNU\0' + bytes(20))
        with self.assertRaises(RuntimeError): self.step()
        self.assertEqual(self.controller.writes, [])

    def test_malformed_usb_selection_fails_without_policy_write(self):
        self.put('/sys/class/power_supply/5-006a/usb_type', 'Unknown SDP CDP DCP')
        with self.assertRaises(RuntimeError): self.step()
        self.assertEqual(self.controller.writes, [])

    def test_adoption_refuses_foreign_policy_or_offline_source(self):
        with self.controller.locked():
            with self.assertRaises(RuntimeError): self.controller.adopt()
            self.controller.policy.write_text('500000')
            self.put('/sys/class/power_supply/5-006a/online', '0')
            with self.assertRaises(RuntimeError): self.controller.adopt()
        self.assertFalse(self.controller.state.exists())
        self.assertEqual(self.controller.writes, [])

    def test_bad_marker_permissions_fail_closed(self):
        self.step()
        self.controller.state.chmod(0o644)
        with self.assertRaises(RuntimeError): self.restore()
        self.assertEqual(self.controller.writes, [500000])

    def test_unit_restart_cleanup_and_ordering(self):
        unit = (HERE.parent / 'systemd/omarchy-usb-host-current.service').read_text()
        for line in ('After=omarchy-hw.service', 'RuntimeDirectoryMode=0700',
                     'RuntimeDirectoryPreserve=restart', 'Restart=on-failure',
                     'ExecStopPost=/opt/omarchy-tablet/diagnostics/usb-host-current.py --restore'):
            self.assertIn(line + '\n', unit)
        self.assertNotIn('Before=', unit)
        self.assertNotIn('--adopt-existing', unit)


if __name__ == '__main__':
    unittest.main()
