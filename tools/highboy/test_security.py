#!/usr/bin/env python3
"""Exercise rejection of insecure, mixed-target and private-key security configurations."""
from pathlib import Path
import tempfile
import unittest
import security


class SecurityAuditTests(unittest.TestCase):
    def config(self, target='p4'):
        return dict(security.REQUIRED, IDF_TARGET='"esp32' + target + '"')

    def test_both_targets_and_unprovisioned_result(self):
        for target in security.VERSIONS:
            report = security.audit(target, self.config(target))
            self.assertFalse(report['production_keys_provisioned'])
            self.assertFalse(report['efuses_written'])
            self.assertFalse(report['normal_app_updater_supported'])

    def test_each_security_requirement_is_enforced(self):
        for field in security.REQUIRED:
            with self.subTest(field=field):
                config = self.config(); config[field] = 'n'
                with self.assertRaises(ValueError): security.audit('p4', config)

    def test_unsafe_and_automatic_signing_settings(self):
        for field in security.FORBIDDEN:
            with self.subTest(field=field):
                config = self.config(); config[field] = 'y'
                with self.assertRaises(ValueError): security.audit('p4', config)
        config = self.config(); config['SECURE_BOOT_SIGNING_KEY'] = '"private.pem"'
        with self.assertRaises(ValueError): security.audit('p4', config)

    def test_swapped_target_and_board_overlay(self):
        with self.assertRaises(ValueError): security.audit('c5', self.config())
        config = self.config('c5')
        baseline = security.config_values(security.ROOT / 'firmware_c5/sdkconfig.baseline')
        baseline.update(config)
        config.update(baseline)
        security.audit('c5', config, baseline)
        config['SPIRAM_MALLOC_RESERVE_INTERNAL'] = '0'
        with self.assertRaisesRegex(ValueError, 'overlay'): security.audit('c5', config, baseline)

    def test_duplicate_active_config(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'sdkconfig'
            path.write_text('CONFIG_SECURE_BOOT=y\nCONFIG_SECURE_BOOT=n\n')
            with self.assertRaises(ValueError): security.config_values(path)


if __name__ == '__main__': unittest.main()
