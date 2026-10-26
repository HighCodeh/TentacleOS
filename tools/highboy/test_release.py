#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
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

"""Static memory/compiler and SD OTA regressions; fixture images never access a device."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import build_evidence
import ota_package
import package
import verify_package


def raw_memory(chip, free=None):
    free = build_evidence.MIN_LINKER_FREE[chip] if free is None else free
    if chip == 'P4':
        return {'diram_total': free + 1024, 'used_diram': 1024, 'diram_remain': free}
    return {'version': '1.2', 'layout': [
        {'name': 'Flash', 'total': 0, 'used': 99999, 'free': 0},
        {'name': 'HP SRAM', 'total': free + 1024, 'used': 1024, 'free': free},
        {'name': 'LP SRAM', 'total': 16384, 'used': 24, 'free': 16360}]}


def evidence(chip, target):
    raw = raw_memory(chip)
    return {'schema_version': 1, 'target': chip, 'idf_version': target['idf_version'],
            'status': 'static_build_evidence_only', 'hardware_validated': False,
            'runtime_performance_measured': False, 'efuses_written': False,
            'bytes': target['bytes'], **{field: target[field] for field in
                ('sha256', 'elf_sha256', 'sdkconfig_sha256', 'compile_commands_sha256',
                 'partition_table_sha256')}, 'map_sha256': '1' * 64,
            'sdk_size_report': raw, 'memory': build_evidence.normalize_memory(chip, raw),
            'compiler_policies': {name: {'optimization': policy[0], 'lto': policy[1]}
                                 for name, policy in build_evidence.POLICIES[chip].items()}}


class EvidenceTests(unittest.TestCase):
    def test_p4_idf61_json2_memory(self):
        raw = {'version': '1.2', 'layout': [
            {'name': 'Flash', 'total': 0, 'used': 1000000, 'free': 0},
            {'name': 'DIRAM', 'total': 524288, 'used': 327680, 'free': 196608}]}
        result = build_evidence.normalize_memory('P4', raw)
        self.assertEqual(result['linker_free_bytes'], 196608)
        raw['layout'][1]['name'] = 'External RAM'
        with self.assertRaises(ValueError):
            build_evidence.normalize_memory('P4', raw)

    def test_normalizes_both_sdk_formats_without_counting_flash_or_lp_as_heap(self):
        for chip in ('P4', 'C5'):
            memory = build_evidence.normalize_memory(chip, raw_memory(chip))
            self.assertEqual(memory['used_bytes'], 1024)
            self.assertEqual(memory['linker_free_bytes'], build_evidence.MIN_LINKER_FREE[chip])
            self.assertIs(memory['runtime_heap_measured'], False)

    def test_regression_below_each_floor_is_rejected(self):
        for chip in ('P4', 'C5'):
            with self.subTest(chip=chip), self.assertRaisesRegex(ValueError, 'regression'):
                build_evidence.normalize_memory(chip, raw_memory(chip, build_evidence.MIN_LINKER_FREE[chip] - 1))

    def test_invalid_counts_and_missing_regions_fail(self):
        for field, value in [('diram_total', True), ('diram_remain', -1), ('used_diram', 0)]:
            raw = raw_memory('P4'); raw[field] = value
            with self.assertRaises(ValueError): build_evidence.normalize_memory('P4', raw)
        for layout in ([], [raw_memory('C5')['layout'][1]] * 2):
            with self.assertRaises(ValueError):
                build_evidence.normalize_memory('C5', {'version': '1.2', 'layout': layout})

    def test_effective_last_flags_preserve_critical_paths_and_opaque_crypto(self):
        for chip, policies in build_evidence.POLICIES.items():
            commands = [{'file': '/fixture/firmware_' + chip.lower() + '/' + name,
                         'arguments': ['cc', '-O0', '-flto=2', opt, '-flto=2' if lto else '-fno-lto']}
                        for name, (opt, lto) in policies.items()]
            result = build_evidence.compiler_policies(chip, commands)
            self.assertEqual(len(result), len(policies))
            changed = copy.deepcopy(commands); changed[0]['arguments'].append('-Os')
            with self.assertRaisesRegex(ValueError, 'optimization'): build_evidence.compiler_policies(chip, changed)
            changed = copy.deepcopy(commands); changed[0]['arguments'].append('-flto')
            with self.assertRaisesRegex(ValueError, 'LTO'): build_evidence.compiler_policies(chip, changed)
            with self.assertRaisesRegex(ValueError, 'duplicate'):
                build_evidence.compiler_policies(chip, commands + [commands[0]])
            with self.assertRaisesRegex(ValueError, 'Missing'):
                build_evidence.compiler_policies(chip, commands[1:])


class OtaPackageTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='highboy-release-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.report = {'schema_version': 1, 'protocol_version': 8, 'app_only': True,
                       'status': 'compiled_candidate_requires_hardware_acceptance',
                       'flashed': False, 'assets_rebuilt': False, 'runtime_validated': False,
                       'partition_migration': False, 'host_tests_passed': True, 'targets': {},
                       'app_signing': {'production_mode': False, 'custom_public_key': False,
                                      'key_custody_verified': False, 'trust_store_sha256': '0' * 64}}
        images = {}
        for chip, profile in verify_package.PROFILE.items():
            image = self.root / profile['file']; image.write_bytes(chip.encode() * 128)
            images[chip] = image
            target = {**profile, 'bytes': image.stat().st_size,
                      'free_bytes': profile['slot_bytes'] - image.stat().st_size,
                      'sha256': verify_package.digest(image), 'elf_sha256': '2' * 64,
                      'sdkconfig_sha256': '3' * 64, 'compile_commands_sha256': '4' * 64}
            target['build_evidence'] = evidence(chip, target)
            self.report['targets'][chip] = target
        self.candidate = package.write_package(self.report, images, self.root / 'candidates')
        self.output = self.root / 'ota'

    def prepare(self):
        return ota_package.prepare(self.candidate, self.output)

    def test_roundtrip_pair_correct_sd_paths_and_immutable_repeat(self):
        directory = self.prepare()
        report = ota_package.verify(directory)
        self.assertEqual(self.prepare(), directory)
        self.assertIs(report['production_release'], False)
        for chip, name in ota_package.SD_FILES.items():
            self.assertEqual((directory / name).read_bytes(),
                             (self.candidate / self.report['targets'][chip]['file']).read_bytes())

    def test_missing_memory_evidence_requires_repackaging(self):
        report = copy.deepcopy(self.report)
        for target in report['targets'].values(): target.pop('build_evidence')
        (self.candidate / 'manifest.json').write_text(json.dumps(report))
        with self.assertRaisesRegex(ValueError, 'evidence required'): self.prepare()
        self.assertFalse(self.output.exists())

    def test_memory_artifact_binding_and_fabricated_acceptance_rejected(self):
        for field, value in [('sha256', '9' * 64), ('elf_sha256', '9' * 64),
                             ('sdkconfig_sha256', '9' * 64), ('target', 'C5'),
                             ('hardware_validated', True), ('runtime_performance_measured', True),
                             ('efuses_written', True), ('idf_version', 'v0.0.0')]:
            report = copy.deepcopy(self.report)
            report['targets']['P4']['build_evidence'][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                verify_package.validate_report(report)

    def test_sd_corruption_and_crossed_targets_are_rejected(self):
        directory = self.prepare()
        p4, c5 = [directory / ota_package.SD_FILES[chip] for chip in ('P4', 'C5')]
        p4.write_bytes(c5.read_bytes())
        with self.assertRaisesRegex(ValueError, 'SD image'): ota_package.verify(directory)
        with self.assertRaises(ValueError): self.prepare()

    def test_symlinked_parent_and_extra_flash_payload_are_rejected(self):
        directory = self.prepare()
        (directory / 'bootloader.bin').write_bytes(b'not an OTA artifact')
        with self.assertRaisesRegex(ValueError, 'Unexpected'): ota_package.verify(directory)
        (directory / 'bootloader.bin').unlink()
        sd = directory / 'sdcard'; external = self.root / 'sd'; sd.rename(external); sd.symlink_to(external)
        with self.assertRaisesRegex(ValueError, 'Symlink'): ota_package.verify(directory)

    def test_approval_flags_and_instruction_changes_are_rejected(self):
        directory = self.prepare(); path = directory / 'ota.json'; original = path.read_text()
        for field in ota_package.BOUNDARY:
            value = json.loads(original)
            if type(value[field]) is not bool: continue
            value[field] = not value[field]; path.write_text(json.dumps(value))
            with self.subTest(field=field), self.assertRaises(ValueError): ota_package.verify(directory)
        path.write_text(original)
        (directory / 'INSTALL.txt').write_text('flash all targets now')
        with self.assertRaisesRegex(ValueError, 'contract changed'): ota_package.verify(directory)

    def test_failed_copy_never_publishes_partial_package(self):
        with mock.patch.object(ota_package.shutil, 'copyfile', side_effect=OSError('copy failed')):
            with self.assertRaises(OSError): self.prepare()
        self.assertEqual(list(self.output.iterdir()), [])

    def test_source_changed_during_copy_is_rejected(self):
        original = ota_package.shutil.copyfile
        def corrupt_copy(source, destination):
            result = original(source, destination)
            if Path(destination).suffix == '.bin': Path(destination).write_bytes(b'corruption')
            return result
        with mock.patch.object(ota_package.shutil, 'copyfile', side_effect=corrupt_copy):
            with self.assertRaises(ValueError): self.prepare()
        self.assertEqual(list(self.output.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
