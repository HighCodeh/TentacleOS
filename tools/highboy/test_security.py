#!/usr/bin/env python3
"""Exercise rejection of insecure, mixed-target and private-key security configurations."""
from pathlib import Path
import tempfile
import unittest
import hashlib
import json
import secure_build
import verify_package
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


class FactoryBuildAuditTests(unittest.TestCase):
    def test_original_and_factory_tables(self):
        for target in ('p4', 'c5'):
            data, original = secure_build.partition_table(security.ROOT/f'firmware_{target}/partitions.csv')
            self.assertEqual(hashlib.sha256(data).hexdigest(), verify_package.PROFILE[target.upper()]['partition_table_sha256'])
            _, factory = secure_build.partition_table(security.ROOT/f'integration/security/partitions-{target}.csv')
            for name in ('ota_0', 'ota_1', 'assets', 'coredump'):
                self.assertEqual(factory[name][:4], original[name][:4])
            self.assertEqual(factory['nvs_keys'][2:5], (0x1a000, 0x1000, 1))
            self.assertEqual(factory['nvs'][4], 0)
            self.assertEqual(factory['assets'][4], 1)
            self.assertEqual(factory['coredump'][4], 1)

    def test_budget_reserves_real_signature_sector(self):
        self.assertEqual(secure_build.signature_budget(0x5000, 0x6000, 0x1000)['signed_free_bytes'], 0)
        for length, capacity, alignment in ((0x6000, 0x6000, 0x1000),
                                            (0x2d0000, 0x2d0000, 0x10000),
                                            (0x200000, 0x200000, 0x10000), (0x5001, 0xe000, 0x1000),
                                            (0, 0xe000, 0x1000)):
            with self.subTest(length=length), self.assertRaises(ValueError):
                secure_build.signature_budget(length, capacity, alignment)
        secure_build.signature_budget(0x2c0000, 0x2d0000, 0x10000)
        secure_build.signature_budget(0x1f0000, 0x200000, 0x10000)

    def test_unordered_overlap_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'table.csv'
            path.write_text('nvs, data, nvs, 0x11000, 24K,\nnvs_keys, data, nvs_keys, 0x1a000, 4K, encrypted\notadata, data, ota, 0x17000, 8K,\n')
            with self.assertRaisesRegex(ValueError, 'unordered'):
                secure_build.partition_table(path)

    def test_factory_audit_and_mixed_layout_rejection(self):
        for target in ('p4', 'c5'):
            config = dict(security.REQUIRED, IDF_TARGET=json.dumps('esp32'+target),
                          PARTITION_TABLE_OFFSET='0x10000',
                          PARTITION_TABLE_CUSTOM_FILENAME=json.dumps('../integration/security/partitions-'+target+'.csv'),
                          NVS_ENCRYPTION='y', NVS_SEC_KEY_PROTECT_USING_FLASH_ENC='y')
            security.audit(target, config, factory=True)
            for name in ('PARTITION_TABLE_OFFSET', 'PARTITION_TABLE_CUSTOM_FILENAME', 'NVS_ENCRYPTION',
                         'NVS_SEC_KEY_PROTECT_USING_FLASH_ENC'):
                bad=dict(config);bad[name]='n'
                with self.subTest(name=name), self.assertRaises(ValueError):
                    security.audit(target, bad, factory=True)

    def test_factory_preserves_every_c5_overlay_setting(self):
        baseline = dict(security.REQUIRED, IDF_TARGET='"esp32c5"',
                        PARTITION_TABLE_OFFSET='0x8000', PARTITION_TABLE_CUSTOM_FILENAME='"partitions.csv"')
        overlay = security.config_values(security.ROOT/'firmware_c5/sdkconfig.baseline')
        baseline.update(overlay)
        config = dict(baseline, PARTITION_TABLE_OFFSET='0x10000',
                      PARTITION_TABLE_CUSTOM_FILENAME='"../integration/security/partitions-c5.csv"',
                      NVS_ENCRYPTION='y', NVS_SEC_KEY_PROTECT_USING_FLASH_ENC='y')
        security.audit('c5', config, baseline, factory=True)
        for name, value in overlay.items():
            if name == 'PARTITION_TABLE_OFFSET':continue
            bad=dict(config);bad[name]='n' if value=='y' else '0'
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'overlay'):
                security.audit('c5', bad, baseline, factory=True)

    def test_failed_or_approved_review_is_not_accepted(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'build.json'
            for bad in ({'status':'failed'}, dict(secure_build.BOUNDARY, deployment_approved=True)):
                path.write_text(json.dumps(bad))
                with self.assertRaises(ValueError):secure_build.verify(temporary)
        with self.assertRaises(ValueError):secure_build.outside_checkout(security.ROOT/'firmware_p4/build_highboy')


if __name__ == '__main__': unittest.main()
