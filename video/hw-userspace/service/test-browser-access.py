#!/usr/bin/env python3
"""Offline policy checks; no device, sysfs or tablet operations."""
import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('access', here / 'browser-access.py')
access = importlib.util.module_from_spec(spec)
spec.loader.exec_module(access)
real_evidence = (here.parents[1] / 'evidence/guard-runtime-first-boot.txt').read_text()


class PolicyTests(unittest.TestCase):
    def test_real_zero_count_evidence_passes(self):
        self.assertEqual(access.validate_guard(real_evidence, 0)['capture']['dmabuf_caps'], '0x00000014')

    def test_stock_mmap_capability_is_refused(self):
        with self.assertRaises(RuntimeError):
            access.validate_guard(real_evidence.replace('capabilities=00000014', 'capabilities=00000015'), 0)

    def test_wrong_queue_or_missing_queue_is_refused(self):
        for changed in (real_evidence.replace('type=9 ', 'type=11 '), real_evidence.split('ENTER REQBUFS type=9')[0]):
            with self.assertRaises(RuntimeError):
                access.validate_guard(changed, 0)

    def test_nonzero_count_or_probe_failure_is_refused(self):
        with self.assertRaises(RuntimeError):
            access.validate_guard(real_evidence.replace('count=0', 'count=1'), 0)
        with self.assertRaises(RuntimeError):
            access.validate_guard(real_evidence, 1)

    def test_no_opt_in_withdraws_stale_ready_without_grant(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            ready = base / 'ready'
            ready.write_text('stale')
            with patch.object(access, 'READY', ready), patch.object(access, 'ENABLE', base / 'disabled'), \
                 patch.object(access, 'HEAP', base / 'absent-heap'), patch.object(access, 'decoder_nodes', return_value=iter([])), \
                 patch.object(access, 'permission') as permission:
                access.grant()
                self.assertFalse(ready.exists())
                permission.assert_not_called()

    def test_preflight_failure_never_grants_and_locks_both_devices(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            enabled = base / 'enabled'; enabled.touch()
            ready = base / 'ready'; ready.write_text('stale')
            device = base / 'video0'; device.touch()
            heap = base / 'heap'; heap.touch()
            with patch.object(access, 'READY', ready), patch.object(access, 'ENABLE', enabled), \
                 patch.object(access, 'HEAP', heap), patch.object(access, 'decoder_nodes', side_effect=lambda: iter([device])), \
                 patch.object(access, 'permission') as permission, patch.object(access.platform, 'system', return_value='Unsupported'):
                with self.assertRaises(RuntimeError): access.grant()
                self.assertFalse(ready.exists())
                self.assertTrue(permission.call_args_list)
                self.assertTrue(all(call.args[1:] == (0, 0o600) for call in permission.call_args_list))

    def test_public_ready_marker_is_atomic_readable_and_has_boot_id(self):
        with tempfile.TemporaryDirectory() as directory:
            ready = Path(directory) / 'ready'
            with patch.object(access, 'READY', ready):
                access.publish({'boot_id': 'test-boot', 'version': 1})
            self.assertEqual(ready.stat().st_mode & 0o777, 0o644)
            self.assertIn('test-boot', ready.read_text())
            self.assertEqual(list(ready.parent.iterdir()), [ready])

    def test_unit_covers_startup_failure_stop_and_kill_cleanup(self):
        unit = (here / 'omarchy-tablet-vcu.service').read_text()
        self.assertIn('Type=notify\n', unit)
        self.assertIn('KillMode=control-group\n', unit)
        for directive, action in [('ExecStartPre', '--lock'), ('ExecStartPost', '--grant'),
                                  ('ExecStop', '--lock'), ('ExecStopPost', '--lock')]:
            self.assertIn(f'{directive}=/opt/omarchy-tablet/video/runtime/browser-access.py {action}\n', unit)


if __name__ == '__main__': unittest.main()
