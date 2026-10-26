#!/usr/bin/env python3
"""Private review-bundle integrity tests with explicit synthetic flash/image fixtures."""
import binascii
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import recovery
from fixtures.recovery_tables import TABLES


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='recovery-test-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.candidate = self.root / 'candidate'
        self.candidate.mkdir()
        self.report = {'schema_version':1,'protocol_version':2,'app_only':True,
            'status':'compiled_candidate_requires_hardware_acceptance','flashed':False,
            'assets_rebuilt':False,'runtime_validated':False,'partition_migration':False,
            'host_tests_passed':True,'targets':{},'app_signing':{'production_mode':False,
                'custom_public_key':False,'key_custody_verified':False,'trust_store_sha256':'0'*64}}
        self.backups = {}
        for chip, profile in recovery.verify_package.PROFILE.items():
            image = (chip + ' candidate').encode()
            (self.candidate / profile['file']).write_bytes(image)
            self.report['targets'][chip] = dict(profile, bytes=len(image), free_bytes=profile['slot_bytes']-len(image), sha256=recovery.digest(image))
            directory = self.root / chip; directory.mkdir()
            flash = bytearray(b'\xff' * recovery.FLASH_SIZE)
            flash[recovery.TABLE_OFFSET:recovery.TABLE_OFFSET + recovery.TABLE_SIZE] = TABLES[chip]
            assets, _ = recovery.ASSETS[chip]; flash[assets] = 0x42
            backup = directory / 'flash.bin'; backup.write_bytes(flash); self.backups[chip] = backup
            self.metadata(chip)
        (self.candidate / 'manifest.json').write_text(json.dumps(self.report))
        # Real esptool validation is exercised separately against actual built images.
        self.images = patch.object(recovery, 'verify_image', side_effect=self.image_fixture)
        self.images.start(); self.addCleanup(self.images.stop)

    def image_fixture(self, data, chip):
        self.assertIn(chip, ('P4','C5'))
        self.assertTrue(data)

    def metadata(self, chip):
        data = self.backups[chip].read_bytes()
        (self.backups[chip].parent / 'manifest.json').write_text(json.dumps({
            'chip':'esp32'+chip.lower(), 'backup_bytes':len(data),
            'backup_sha256':recovery.digest(data), 'status':'checked'}))

    def prepare(self):
        return recovery.prepare(self.candidate, self.backups, self.root / 'review')

    def test_private_pair_and_repeated_verification(self):
        output = self.prepare(); report = recovery.verify(output)
        self.assertFalse(report['deployment_approved'])
        self.assertFalse(report['assets_filesystem_validated'])
        self.assertTrue(report['contains_private_device_data'])
        self.assertEqual(output.stat().st_mode & 0o777, 0o700)
        for path in output.rglob('*'):
            self.assertEqual(path.stat().st_mode & 0o777, 0o700 if path.is_dir() else 0o600)
        self.assertEqual(report['targets']['P4']['app_update_offset'], 0x20000)
        self.assertEqual(report['targets']['C5']['snapshot_sha256'], recovery.digest(self.backups['C5'].read_bytes()))
        with self.assertRaises(ValueError): self.prepare()

    def test_corrupted_snapshot_metadata_and_map(self):
        data = bytearray(self.backups['P4'].read_bytes()); data[0] ^= 1
        self.backups['P4'].write_bytes(data)
        with self.assertRaisesRegex(ValueError,'capture metadata'): self.prepare()
        data[recovery.TABLE_OFFSET] ^= 1; self.backups['P4'].write_bytes(data); self.metadata('P4')
        with self.assertRaisesRegex(ValueError,'installed map'): self.prepare()
        self.assertFalse((self.root/'review').exists())

    def test_blank_assets_and_pending_ota(self):
        data = bytearray(self.backups['C5'].read_bytes())
        offset, size = recovery.ASSETS['C5']; data[offset:offset+size] = b'\xff'*size
        self.backups['C5'].write_bytes(data);self.metadata('C5')
        with self.assertRaisesRegex(ValueError,'assets'): self.prepare()
        data[offset] = 0x42
        sequence = 2
        struct.pack_into('<I',data,recovery.OTA_OFFSET,sequence)
        crc = binascii.crc32(struct.pack('<I',sequence),0xffffffff)&0xffffffff
        struct.pack_into('<II',data,recovery.OTA_OFFSET+24,1,crc)
        self.backups['C5'].write_bytes(data);self.metadata('C5')
        with self.assertRaisesRegex(ValueError,'Pending'): self.prepare()
        struct.pack_into('<I',data,recovery.OTA_OFFSET+24,2)
        self.backups['C5'].write_bytes(data);self.metadata('C5')
        output=self.prepare()
        self.assertEqual(recovery.verify(output)['targets']['C5']['app_update_offset'],0x220000)

    def test_output_tampering_and_fabricated_approval(self):
        output=self.prepare(); path=output/'recovery.json'
        report=json.loads(path.read_text());report['deployment_approved']=True;path.write_text(json.dumps(report))
        with self.assertRaisesRegex(ValueError,'boundary'): recovery.verify(output)
        report['deployment_approved']=False;report['targets']['P4']['app_update_offset']=0;path.write_text(json.dumps(report))
        with self.assertRaisesRegex(ValueError,'offsets'): recovery.verify(output)

    def test_candidate_or_image_error_never_publishes(self):
        self.images.stop()
        with patch.object(recovery,'verify_image',side_effect=ValueError('invalid image')):
            with self.assertRaisesRegex(ValueError,'invalid image'):self.prepare()
        self.images.start()
        self.assertFalse((self.root/'review').exists())
        (self.candidate/'TentacleOS_P4.bin').write_bytes(b'corrupt')
        with self.assertRaises(ValueError):self.prepare()

    def test_missing_peer_and_symlinked_inputs(self):
        with self.assertRaisesRegex(ValueError,'Both chip'):recovery.prepare(self.candidate,{'P4':self.backups['P4']},self.root/'review')
        original=self.backups['P4'];link=original.parent/'link.bin';link.symlink_to(original);self.backups['P4']=link
        with self.assertRaisesRegex(ValueError,'symlinked'):self.prepare()


if __name__=='__main__':unittest.main()
