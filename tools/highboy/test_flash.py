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

"""Offline deployment guards; never opens a serial port."""
import binascii
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import flash


def ota_entry(seq, state=2):
    entry = bytearray(b'\xff' * flash.SECTOR_SIZE)
    crc = binascii.crc32(struct.pack('<I', seq), 0xFFFFFFFF) & 0xFFFFFFFF
    struct.pack_into('<I', entry, 0, seq)
    struct.pack_into('<II', entry, 24, state, crc)
    return bytes(entry)


class OtaTests(unittest.TestCase):
    def test_erased_metadata_boots_first_slot(self):
        self.assertEqual(flash.active_slot(b'\xff' * 8192), 0)

    def test_newest_valid_entry_selects_slot(self):
        self.assertEqual(flash.active_slot(ota_entry(3) + ota_entry(4)), 1)

    def test_aborted_entry_uses_previous(self):
        self.assertEqual(flash.active_slot(ota_entry(3) + ota_entry(4, 4)), 0)

    def test_pending_update_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'pending'):
            flash.active_slot(ota_entry(3) + ota_entry(4, 1))

    def test_corrupt_metadata_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'invalid'):
            flash.active_slot(b'\x00' * 8192)


class LayoutFixture:
    target = 'p4'
    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, str(Path(sys.argv[-1]) / 'components/partition_table'))
        import gen_esp32part
        cls.gen = gen_esp32part

    def setUp(self):
        self.table = self.gen.PartitionTable.from_csv(
            (flash.ROOT / f'firmware_{self.target}/partitions.csv').read_text()).to_binary()
        self.data = bytearray(b'\xff' * 0x800000)
        self.data[flash.TABLE_OFFSET:flash.TABLE_OFFSET + len(self.table)] = self.table
        assets, = [p for p in self.gen.PartitionTable.from_binary(self.table) if p.name == 'assets']
        self.data[assets.offset:assets.offset + 4] = b'test'


class LayoutTests(LayoutFixture, unittest.TestCase):
    def test_matching_layout_selects_ota_zero(self):
        with patch.object(flash, 'image_valid') as verify:
            slot = flash.validate_device(self.data, self.table, self.gen)
        self.assertEqual(slot.offset, 0x20000)
        self.assertEqual(verify.call_count, 2)

    def test_ota_one_selected_from_installed_metadata(self):
        self.data[0xF000:0x11000] = ota_entry(3) + ota_entry(4)
        with patch.object(flash, 'image_valid'):
            slot = flash.validate_device(self.data, self.table, self.gen)
        self.assertEqual(slot.offset, 0x2F0000)

    def test_layout_mismatch_precedes_image_checks(self):
        self.data[flash.TABLE_OFFSET] ^= 1
        with patch.object(flash, 'image_valid') as verify:
            with self.assertRaisesRegex(ValueError, 'differs'):
                flash.validate_device(self.data, self.table, self.gen)
        verify.assert_not_called()

    def test_erased_assets_rejected(self):
        self.data[0x5D0000:0x5D0004] = b'\xff' * 4
        with patch.object(flash, 'image_valid'):
            with self.assertRaisesRegex(ValueError, 'Assets partition is empty'):
                flash.validate_device(self.data, self.table, self.gen)

    def test_truncated_flash_rejected(self):
        with self.assertRaisesRegex(ValueError, 'beyond'):
            flash.validate_device(self.data[:0x400000], self.table, self.gen)


class FlowTests(LayoutFixture, unittest.TestCase):
    def exercise(self, check=False, security=True, backup_error=None, short_chunk=False):
        commands = []

        def transport(args, *command, reset=False):
            commands.append((command, reset))
            if command[0] == 'get_security_info':
                return ('Secure Boot: Disabled\nFlash Encryption: Disabled' if security
                        else 'Secure Boot: Enabled\nFlash Encryption: Disabled')
            if command[0] == 'flash_id':
                return 'Detected flash size: 8MB'
            if command[0] == 'read_flash':
                if backup_error:
                    raise backup_error
                offset, size = int(command[-3], 0), int(command[-2], 0)
                if short_chunk:
                    size -= 1
                Path(command[-1]).write_bytes(self.data[offset:offset + size])
            return ''

        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            project = root / f'firmware_{self.target}'
            build = project / 'build_highboy'
            (build / 'partition_table').mkdir(parents=True)
            (build / f'TentacleOS_{self.target.upper()}.bin').write_bytes(b'candidate')
            (build / 'partition_table/partition-table.bin').write_bytes(self.table)
            (project / 'partitions.csv').write_text(
                (flash.ROOT / f'firmware_{self.target}/partitions.csv').read_text())
            import check_contracts
            args = ['flash.py', '--idf-path', sys.argv[-1], '--target', self.target, '-p', '/dev/test']
            if check:
                args.append('--check')
            with patch.object(flash, 'ROOT', root), patch.object(flash, 'image_valid'), \
                    patch.object(flash, 'run_tool', side_effect=transport), \
                    patch.object(check_contracts, 'main'), patch.object(sys, 'argv', args):
                try:
                    flash.main()
                except Exception:
                    self.commands = commands
                    raise
            manifest, = (root / 'integration/device_backups').glob('*/manifest.json')
            import json
            report = json.loads(manifest.read_text())
        return commands, report

    def test_check_never_writes(self):
        commands, report = self.exercise(check=True)
        self.assertNotIn('write_flash', [command[0] for command, _ in commands])
        self.assertEqual(report['status'], 'checked')
        self.assertTrue(commands[-1][1])

    def test_write_only_selected_app_after_backup_verify(self):
        self.data[0xF000:0x11000] = ota_entry(3) + ota_entry(4)
        commands, report = self.exercise()
        chunks = flash.FLASH_SIZE // flash.BACKUP_CHUNK_SIZE
        self.assertEqual([command[0] for command, _ in commands],
                         ['get_security_info', 'flash_id'] + ['read_flash'] * chunks +
                         ['verify_flash', 'write_flash', 'verify_flash', 'read_mac'])
        self.assertEqual(commands[-3][0][1], '0x2f0000')
        self.assertFalse(commands[-3][1])
        self.assertTrue(commands[-1][1])
        self.assertEqual(report['status'], 'written_and_verified')

    def test_security_rejection_precedes_backup_and_write(self):
        with self.assertRaisesRegex(ValueError, 'requires Secure Boot'):
            self.exercise(security=False)
        self.assertEqual([command[0] for command, _ in self.commands],
                         ['get_security_info', 'read_mac'])

    def test_layout_rejection_never_writes(self):
        self.data[flash.TABLE_OFFSET] ^= 1
        with self.assertRaisesRegex(ValueError, 'differs'):
            self.exercise()
        self.assertNotIn('write_flash', [command[0] for command, _ in self.commands])

    def test_backup_timeout_never_writes_and_attempts_reset(self):
        with self.assertRaises(subprocess.TimeoutExpired):
            self.exercise(backup_error=subprocess.TimeoutExpired('esptool', 120))
        self.assertNotIn('write_flash', [command[0] for command, _ in self.commands])
        self.assertEqual(self.commands[-1], (('read_mac',), True))

    def test_short_backup_chunk_never_writes(self):
        with self.assertRaisesRegex(ValueError, 'Incomplete flash backup chunk'):
            self.exercise(short_chunk=True)
        self.assertNotIn('write_flash', [command[0] for command, _ in self.commands])


class C5Tests(LayoutFixture, unittest.TestCase):
    target = 'c5'
    exercise = FlowTests.exercise

    def test_c5_second_slot_uses_c5_map(self):
        self.data[0xF000:0x11000] = ota_entry(3) + ota_entry(4)
        commands, report = self.exercise()
        self.assertEqual(report['chip'], 'esp32c5')
        self.assertEqual(report['offset'], 0x220000)
        self.assertEqual(commands[-3][0][1], '0x220000')
        self.assertFalse(report['other_chip_updated'])

    def test_c5_check_never_writes(self):
        commands, report = self.exercise(check=True)
        self.assertNotIn('write_flash', [command[0] for command, _ in commands])
        self.assertEqual(report['chip'], 'esp32c5')
        self.assertEqual(report['status'], 'checked')

    def test_c5_rejects_p4_slot_profile(self):
        with self.assertRaisesRegex(ValueError, 'Unexpected app slot size'):
            flash.validate_device(self.data, self.table, self.gen, 'p4')


class TransportTests(unittest.TestCase):
    def test_chip_and_no_stub_match_target(self):
        from types import SimpleNamespace
        for target in ('p4', 'c5'):
            args = SimpleNamespace(target=target, port='/dev/test', baud=460800)
            with patch.object(flash.subprocess, 'run') as tool:
                tool.return_value = SimpleNamespace(returncode=0, stdout='')
                flash.run_tool(args, 'read_mac', reset=True)
                command = tool.call_args.args[0]
                self.assertEqual(command[command.index('--chip') + 1], 'esp32' + target)
                self.assertEqual('--no-stub' in command, target == 'c5')


if __name__ == '__main__':
    # Pass the SDK path without giving it to unittest's argument parser.
    sdk = sys.argv.pop()
    with patch.object(sys, 'argv', [sys.argv[0], sdk]):
        unittest.main(argv=[sys.argv[0]])
