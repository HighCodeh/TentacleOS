#!/usr/bin/env python3
# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

"""Package/verification regressions with explicit artifact fixtures; no device access."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import package
import verify_package


class PackageTests(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory(prefix='candidate-test-')
        self.addCleanup(temporary.cleanup)
        self.root=Path(temporary.name)
        self.images={}
        self.report={'schema_version':1,'protocol_version':2,'spi_transport_revision':1,'app_only':True,
            'status':'compiled_candidate_requires_hardware_acceptance','flashed':False,
            'assets_rebuilt':False,'runtime_validated':False,'partition_migration':False,
            'host_tests_passed':True,'targets':{},'app_signing':{'production_mode':False,
                'custom_public_key':False,'key_custody_verified':False,'trust_store_sha256':'0'*64}}
        for chip,profile in verify_package.PROFILE.items():
            image=self.root/profile['file'];image.write_bytes(chip.encode()*128)
            self.images[chip]=image
            self.report['targets'][chip]={**profile,'bytes':image.stat().st_size,
                'free_bytes':profile['slot_bytes']-image.stat().st_size,'sha256':verify_package.digest(image)}

    def write(self,report=None):
        return package.write_package(self.report if report is None else report,self.images,self.root/'packages')

    def test_verified_pair_and_repeat_are_immutable(self):
        directory=self.write()
        self.assertEqual(verify_package.verify(directory),self.report)
        self.assertEqual(self.write(),directory)
        self.assertEqual(set(p.name for p in directory.iterdir()),
                         {'manifest.json','TentacleOS_P4.bin','TentacleOS_C5.bin'})
        before=(directory/'manifest.json').read_bytes()
        changed=copy.deepcopy(self.report);changed['source_commit']='fixture changed'
        self.assertNotEqual(self.write(changed),directory)
        self.assertEqual((directory/'manifest.json').read_bytes(),before)

    def test_corrupt_existing_candidate_is_not_overwritten(self):
        directory=self.write();image=directory/'TentacleOS_P4.bin';image.write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError,'length mismatch'):self.write()
        self.assertEqual(image.read_bytes(),b'corrupt')

    def test_bad_copy_never_publishes_partial_directory(self):
        with mock.patch.object(package.shutil,'copyfile',side_effect=OSError('copy failed')):
            with self.assertRaises(OSError):self.write()
        self.assertEqual(list((self.root/'packages').iterdir()),[])

    def test_source_change_during_copy_is_rejected(self):
        self.images['C5'].write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError,'length mismatch'):self.write()
        self.assertEqual(list((self.root/'packages').iterdir()),[])

    def test_reject_wrong_profile_and_fabricated_acceptance(self):
        changes=[('schema_version',2),('protocol_version',7),('protocol_version',8),('spi_transport_revision',0),('spi_transport_revision',None),('runtime_validated',True),
                 ('host_tests_passed',False),('app_only',False),('flashed',True),
                 ('assets_rebuilt',True),('partition_migration',True)]
        for field,value in changes:
            with self.subTest(field=field):
                report=copy.deepcopy(self.report);report[field]=value
                with self.assertRaises(ValueError):self.write(report)
        for field,value in [('file','../image.bin'),('idf_version','v0.0.0'),('slot_bytes',0x300000),
                            ('free_bytes',0),('bytes',True),('partition_table_sha256','0'*64),
                            ('sha256','0'*64)]:
            with self.subTest(field=field):
                report=copy.deepcopy(self.report);report['targets']['P4'][field]=value
                with self.assertRaises(ValueError):self.write(report)
        report=copy.deepcopy(self.report);report['targets'].pop('C5')
        with self.assertRaises(ValueError):self.write(report)

    def test_duplicate_manifest_and_symlinked_image_rejected(self):
        directory=self.write();manifest=directory/'manifest.json';original=manifest.read_text()
        manifest.write_text(original.replace('"protocol_version": 2','"protocol_version": 2, "protocol_version": 7'))
        with self.assertRaisesRegex(ValueError,'Duplicate'):verify_package.verify(directory)
        manifest.write_text(original)
        image=directory/'TentacleOS_C5.bin';image.unlink();image.symlink_to(self.images['C5'])
        with self.assertRaisesRegex(ValueError,'symlinked'):verify_package.verify(directory)

    def test_signing_configuration_uses_only_the_selected_public_header(self):
        project=self.root/'project';build=self.root/'build'
        header=project/'components/Service/app_runtime/include/tos_trust_keys.h'
        header.parent.mkdir(parents=True);header.write_text('development public fixture')
        build.mkdir();(build/'CMakeCache.txt').write_text('TOS_APP_PRODUCTION:BOOL=OFF\nTOS_APP_TRUST_PUBLIC_KEY:STRING=\n')
        selected=package.app_signing_configuration(project,build)
        self.assertFalse(selected['production_mode']);self.assertEqual(selected['trust_store_sha256'],verify_package.digest(header))
        custom=build/'esp-idf/Service/generated/tos_build_trust_keys.h'
        custom.parent.mkdir(parents=True);custom.write_text('custom public fixture')
        (build/'CMakeCache.txt').write_text('TOS_APP_PRODUCTION:BOOL=ON\nTOS_APP_TRUST_PUBLIC_KEY:STRING=public-fixture\n')
        selected=package.app_signing_configuration(project,build)
        self.assertTrue(selected['custom_public_key']);self.assertFalse(selected['key_custody_verified'])
        self.assertEqual(selected['trust_store_sha256'],verify_package.digest(custom))
        report=copy.deepcopy(self.report);report['app_signing']['key_custody_verified']=True
        with self.assertRaises(ValueError):self.write(report)

    def test_link_evidence_respects_lto_without_accepting_unrelated_names(self):
        resolved=package.linked_symbols({'radio.constprop.0','other.isra.1','cmd_subghz'},
            ['radio','other','subghz_protocol_registry_selftest'])
        self.assertEqual(resolved['radio']['linked'],['radio.constprop.0'])
        self.assertEqual(resolved['subghz_protocol_registry_selftest']['kind'],'inlined_console_selftest')
        for names in ({'radio_unrelated'},{'radio.c.fingerprint'},{'radio.constprop.0suffix'}):
            with self.assertRaises(ValueError):package.linked_symbols(names,['radio'])
        with self.assertRaises(ValueError):package.linked_symbols({'other'},['subghz_protocol_registry_selftest'])


if __name__=='__main__':unittest.main()
