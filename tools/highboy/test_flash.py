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
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import flash


def build_metadata(build, project, target, flash_size=0x800000, table_file='partitions.csv'):
    (build / 'config').mkdir(exist_ok=True)
    (build / 'project_description.json').write_text(json.dumps({
        'project_path': str(project.resolve()), 'target': f'esp32{target}'}))
    (build / 'config/sdkconfig.json').write_text(json.dumps({
        'IDF_TARGET': f'esp32{target}', 'ESPTOOLPY_FLASHSIZE': f'{flash_size // (1024 * 1024)}MB',
        'PARTITION_TABLE_CUSTOM_FILENAME': table_file, 'BOOTLOADER_APP_ROLLBACK_ENABLE': True}))
    (build / 'flasher_args.json').write_text(json.dumps({
        'flash_settings': {'flash_size': f'{flash_size // (1024 * 1024)}MB'}}))


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
    flash_size = 0x800000
    table_file = 'partitions.csv'
    @classmethod
    def setUpClass(cls):
        sys.path.insert(0, str(Path(sys.argv[-1]) / 'components/partition_table'))
        import gen_esp32part
        cls.gen = gen_esp32part

    def setUp(self):
        self.table = self.gen.PartitionTable.from_csv(
            (flash.ROOT / f'firmware_{self.target}' / self.table_file).read_text()).to_binary()
        self.data = bytearray(b'\xff' * self.flash_size)
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
        self.assertEqual(slot.offset, 0x330000)

    def test_previous_p4_layout_is_rejected(self):
        from fixtures.recovery_tables import TABLES
        self.data[flash.TABLE_OFFSET:flash.TABLE_OFFSET + flash.TABLE_SIZE] = TABLES['P4']
        with patch.object(flash, 'image_valid') as verify:
            with self.assertRaisesRegex(ValueError, 'No migration or write performed'):
                flash.validate_device(self.data, self.table, self.gen)
            verify.assert_not_called()

    def test_previous_nfc_layout_is_rejected(self):
        old_csv = (flash.ROOT / 'firmware_p4/partitions.csv').read_text()
        for current, previous in [('0x310000', '0x2F0000'), ('0x330000', '0x310000'),
                                  ('0x640000', '0x600000'), ('0x650000', '0x610000'),
                                  ('0x1B0000', '0x1F0000')]:
            old_csv = old_csv.replace(current, previous)
        old_table = self.gen.PartitionTable.from_csv(old_csv).to_binary()
        self.data[flash.TABLE_OFFSET:flash.TABLE_OFFSET + len(old_table)] = old_table
        with patch.object(flash, 'image_valid') as verify:
            with self.assertRaisesRegex(ValueError, 'No migration or write performed'):
                flash.validate_device(self.data, self.table, self.gen)
            verify.assert_not_called()

    def test_previous_three_mib_layout_is_rejected(self):
        table = self.gen.PartitionTable.from_csv(
            (flash.ROOT / 'firmware_c5/partitions.csv').read_text()).to_binary()
        self.data[flash.TABLE_OFFSET:flash.TABLE_OFFSET + len(table)] = table
        with patch.object(flash, 'image_valid') as verify:
            with self.assertRaisesRegex(ValueError, 'No migration or write performed'):
                flash.validate_device(self.data, self.table, self.gen)
            verify.assert_not_called()

    def test_layout_mismatch_precedes_image_checks(self):
        self.data[flash.TABLE_OFFSET] ^= 1
        with patch.object(flash, 'image_valid') as verify:
            with self.assertRaisesRegex(ValueError, 'differs'):
                flash.validate_device(self.data, self.table, self.gen)
        verify.assert_not_called()

    def test_erased_assets_rejected(self):
        self.data[0x650000:0x650004] = b'\xff' * 4
        with patch.object(flash, 'image_valid'):
            with self.assertRaisesRegex(ValueError, 'Assets partition is empty'):
                flash.validate_device(self.data, self.table, self.gen)

    def test_truncated_flash_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Incomplete full flash backup'):
            flash.validate_device(self.data[:0x400000], self.table, self.gen)


class FlowTests(LayoutFixture, unittest.TestCase):
    def exercise(self, check=False, security=True, backup_error=None, short_chunk=False, capacity=None, fail_new_verify=False, mode='app'):
        commands = []
        parts = {p.name: p for p in self.gen.PartitionTable.from_binary(self.table)}
        for name in ('ota_0', 'ota_1'):
            offset = parts[name].offset
            header = bytearray(24)
            header[0:2] = b'\xe9\x01'
            header[23] = 1
            self.data[offset:offset+24] = header
            self.data[offset+24:offset+32] = struct.pack('<II', 0, 4)

        def transport(args, *command, reset=False):
            commands.append((command, reset))
            if command[0] == 'get_security_info':
                return ('Secure Boot: Disabled\nFlash Encryption: Disabled' if security
                        else 'Secure Boot: Enabled\nFlash Encryption: Disabled')
            if command[0] == 'flash_id':
                return f'Detected flash size: {capacity or self.flash_size // (1024 * 1024)}MB'
            if fail_new_verify and command[0] == 'verify_flash' and command[-1].endswith('candidate.bin'):
                raise RuntimeError('candidate digest mismatch')
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
            build = project / 'build'
            (build / 'partition_table').mkdir(parents=True)
            (build / 'bootloader/config').mkdir(parents=True)
            (build / 'bootloader/config/sdkconfig.json').write_text(json.dumps({'BOOTLOADER_APP_ROLLBACK_ENABLE': True}))
            (build / 'bootloader/bootloader.bin').write_bytes(b'boot')
            (build / f'TentacleOS_{self.target.upper()}.bin').write_bytes(b'candidate')
            (build / 'partition_table/partition-table.bin').write_bytes(self.table)
            (project / self.table_file).write_text(
                (flash.ROOT / f'firmware_{self.target}' / self.table_file).read_text())
            build_metadata(build, project, self.target, self.flash_size, self.table_file)
            import check_contracts
            args = ['flash.py', '--idf-path', sys.argv[-1], '--target', self.target, '-p', '/dev/test', '--mode', mode]
            if self.target == 'c5':
                args += ['--control-port', '/dev/p4']
            if check:
                args.append('--check')
            with patch.object(flash, 'ROOT', root), patch.object(flash, 'image_valid'), \
                    patch.object(flash, 'run_tool', side_effect=transport), \
                    patch('ota_flash.verify_bootloader'), \
                    patch('flash_session.confirm_c5_boot') as confirm, \
                    patch('full_flash.prepare', return_value=[('app', 0x20000, b'candidate')]), \
                    patch.object(check_contracts, 'main'), patch.object(sys, 'argv', args):
                try:
                    flash.main()
                    if self.target == 'c5' and not check:
                        confirm.assert_called_once_with('/dev/p4')
                        self.assertTrue(commands[-1][1], 'Confirm only after the reset')
                    else:
                        confirm.assert_not_called()
                except Exception:
                    self.commands = commands
                    raise
            manifest, = (root / 'integration/device_backups').glob('*/manifest.json')
            report = json.loads(manifest.read_text())
        return commands, report

    def test_full_mode_still_reads_complete_backup(self):
        commands, report = self.exercise(check=True, mode='full')
        reads = [command for command, _ in commands if command[0] == 'read_flash']
        self.assertEqual(sum(int(command[-2], 0) for command in reads), self.flash_size)
        self.assertEqual(report['backup_bytes'], self.flash_size)
        self.assertEqual(report['mode'], 'full')
        self.assertNotIn('write_flash', [command[0] for command, _ in commands])

    def test_failed_new_image_verify_does_not_select_it(self):
        with self.assertRaisesRegex(RuntimeError, 'candidate digest mismatch'):
            self.exercise(fail_new_verify=True)
        writes = [command for command, _ in self.commands if command[0] == 'write_flash']
        inactive = 0x620000 if self.flash_size == 0x1000000 else 0x330000
        self.assertEqual([int(command[1], 0) for command in writes], [inactive])

    def test_blank_metadata_initializes_fallback_before_selecting_new(self):
        commands, _ = self.exercise()
        writes = [command for command, _ in commands if command[0] == 'write_flash']
        inactive = 0x620000 if self.flash_size == 0x1000000 else 0x330000
        self.assertEqual([int(command[1], 0) for command in writes], [inactive, 0xf000, 0x10000])

    def test_check_never_writes(self):
        commands, report = self.exercise(check=True)
        self.assertNotIn('write_flash', [command[0] for command, _ in commands])
        self.assertEqual(report['status'], 'checked')
        self.assertTrue(commands[-1][1])

    def test_write_only_selected_app_after_backup_verify(self):
        self.data[0xF000:0x11000] = ota_entry(3) + ota_entry(4)
        commands, report = self.exercise()
        writes = [command for command, _ in commands if command[0] == 'write_flash']
        self.assertEqual([int(command[1], 0) for command in writes], [0x20000, 0xf000])
        self.assertTrue(commands[-1][1])
        self.assertFalse(report['full_backup'])
        self.assertEqual(report['old_slot'], 'ota_1')
        self.assertEqual(report['slot'], 'ota_0')
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
        with self.assertRaisesRegex(ValueError, 'Incomplete OTA preflight read'):
            self.exercise(short_chunk=True)
        self.assertNotIn('write_flash', [command[0] for command, _ in self.commands])


class P4SixteenTests(FlowTests):
    flash_size = 0x1000000
    table_file = 'partitions_16mb.csv'

    def test_write_only_selected_app_after_backup_verify(self):
        super().test_write_only_selected_app_after_backup_verify()

    def test_eight_mb_device_rejected_before_backup(self):
        with self.assertRaisesRegex(ValueError, 'capacity does not match'):
            self.exercise(capacity=8)
        self.assertEqual([command[0] for command, _ in self.commands],
                         ['get_security_info', 'flash_id', 'read_mac'])

    def test_application_does_not_read_full_flash(self):
        commands, report = self.exercise(check=True)
        self.assertFalse(report['full_backup'])
        reads = [command for command, _ in commands if command[0] == 'read_flash']
        self.assertLess(sum(int(command[-2], 0) for command in reads), 65536)


class BuildSelectionTests(unittest.TestCase):
    def test_standard_directory_and_wrong_chip(self):
        with tempfile.TemporaryDirectory() as temp:
            project = Path(temp) / 'firmware_p4'
            build = project / 'build'
            build.mkdir(parents=True)
            (build / 'TentacleOS_P4.bin').write_bytes(b'candidate')
            build_metadata(build, project, 'p4', 0x1000000, 'partitions_16mb.csv')
            selected, profile = flash.select_build(project, 'p4')
            self.assertEqual(selected, build.resolve())
            self.assertEqual(profile[:2], (0x1000000, 0x600000))
            with self.assertRaisesRegex(ValueError, 'standard'):
                flash.select_build(project, 'p4', project / 'alternative')
            build_metadata(build, project, 'c5')
            with self.assertRaisesRegex(ValueError, 'wrong chip'):
                flash.select_build(project, 'p4')

    def test_alternate_directory_does_not_replace_missing_build(self):
        with tempfile.TemporaryDirectory() as temp:
            project = Path(temp)
            alternate = project / 'alternative'
            alternate.mkdir()
            (alternate / 'TentacleOS_P4.bin').write_bytes(b'candidate')
            with self.assertRaisesRegex(ValueError, 'build it first'):
                flash.select_build(project, 'p4')


class C5Tests(LayoutFixture, unittest.TestCase):
    target = 'c5'
    exercise = FlowTests.exercise

    def test_c5_second_slot_uses_c5_map(self):
        self.data[0xF000:0x11000] = ota_entry(3) + ota_entry(4)
        commands, report = self.exercise()
        self.assertEqual(report['chip'], 'esp32c5')
        self.assertEqual(report['offset'], 0x20000)
        self.assertEqual(report['old_slot'], 'ota_1')
        self.assertFalse(report['other_chip_updated'])

    def test_c5_check_never_writes(self):
        commands, report = self.exercise(check=True)
        self.assertNotIn('write_flash', [command[0] for command, _ in commands])
        self.assertEqual(report['chip'], 'esp32c5')
        self.assertEqual(report['status'], 'checked')

    def test_previous_c5_layout_is_rejected(self):
        from fixtures.recovery_tables import TABLES
        self.data[flash.TABLE_OFFSET:flash.TABLE_OFFSET + flash.TABLE_SIZE] = TABLES['C5']
        with patch.object(flash, 'image_valid') as verify:
            with self.assertRaisesRegex(ValueError, 'No migration or write performed'):
                flash.validate_device(self.data, self.table, self.gen, 'c5')
            verify.assert_not_called()


class TransportTests(unittest.TestCase):
    def setUp(self):
        self.console = patch('flash_session.request_p4_boot', return_value=False).start()
        patch('flash_session.request_c5_boot', return_value=False).start()
        self.addCleanup(patch.stopall)

    def test_session_reused_and_reset_without_reconnect(self):
        from types import SimpleNamespace
        from unittest.mock import MagicMock
        import flash_session
        for target in ('p4', 'c5'):
            args = SimpleNamespace(target=target, port='/dev/test', baud=460800, before='no-reset')
            loader = MagicMock()
            factory = MagicMock(return_value=loader)
            api = SimpleNamespace(CHIP_DEFS={f'esp32{target}': factory},
                                  run_stub=MagicMock(return_value=loader), main=MagicMock(),
                                  cmds=SimpleNamespace(reset_chip=MagicMock()))
            with patch.dict(sys.modules, {'esptool': api}):
                flash.run_tool(args, 'get_security_info')
                flash.run_tool(args, 'flash_id')
                flash.run_tool(args, 'verify_flash', '0', 'backup.bin')
                flash.run_tool(args, 'read_mac', reset=True)
            factory.assert_called_once_with('/dev/test', 115200)
            loader.connect.assert_called_once_with('no-reset')
            loader.change_baud.assert_called_once_with(460800)
            api.run_stub.assert_called_once_with(loader)
            for call in api.main.call_args_list:
                argv = call.args[0]
                self.assertEqual(argv[argv.index('--before') + 1], 'no-reset-no-sync')
                self.assertEqual(argv[argv.index('--after') + 1], 'no-reset')
                self.assertIn('--no-stub', argv)
                self.assertIs(call.kwargs['esp'], loader)
            self.assertIn('get-security-info', api.main.call_args_list[0].args[0])
            api.cmds.reset_chip.assert_called_once_with(
                loader, 'watchdog-reset')
            loader._port.close.assert_called_once()

    def test_retains_uploaded_stub_for_reads_and_reset(self):
        from types import SimpleNamespace
        from unittest.mock import MagicMock
        rom, stub = MagicMock(), MagicMock()
        stub._port = rom._port
        api = SimpleNamespace(CHIP_DEFS={'esp32p4': MagicMock(return_value=rom)},
                              run_stub=MagicMock(return_value=stub), main=MagicMock(),
                              cmds=SimpleNamespace(reset_chip=MagicMock()))
        args = SimpleNamespace(target='p4', port='/dev/test', baud=460800, before='no-reset')
        with patch.dict(sys.modules, {'esptool': api}):
            flash.run_tool(args, 'read_flash', '0', '4096', 'backup.bin')
            flash.run_tool(args, 'flash_id')
            flash.run_tool(args, 'read_mac', reset=True)
        api.run_stub.assert_called_once_with(rom)
        for call in api.main.call_args_list:
            self.assertIs(call.kwargs['esp'], stub)
        api.cmds.reset_chip.assert_called_once_with(stub, 'watchdog-reset')
        rom.change_baud.assert_not_called()
        stub.change_baud.assert_called_once_with(460800)

    def test_failed_connect_closes_without_reset_or_reconnect(self):
        from types import SimpleNamespace
        from unittest.mock import MagicMock
        loader = MagicMock()
        loader.connect.side_effect = RuntimeError('connection failed')
        api = SimpleNamespace(CHIP_DEFS={'esp32p4': MagicMock(return_value=loader)},
                              FatalError=RuntimeError, cmds=SimpleNamespace(reset_chip=MagicMock()))
        args = SimpleNamespace(target='p4', port='/dev/test', baud=115200)
        with patch.dict(sys.modules, {'esptool': api}):
            with self.assertRaisesRegex(RuntimeError, 'connection failed'):
                flash.run_tool(args, 'get_security_info')
            flash.run_tool(args, 'read_mac', reset=True)
        loader.connect.assert_called_once()
        api.cmds.reset_chip.assert_not_called()
        loader._port.close.assert_called_once()

    def test_failure_still_allows_cleanup_on_same_session(self):
        from types import SimpleNamespace
        from unittest.mock import MagicMock
        loader = MagicMock()
        api = SimpleNamespace(CHIP_DEFS={'esp32p4': MagicMock(return_value=loader)},
                              run_stub=MagicMock(return_value=loader),
                              main=MagicMock(side_effect=SystemExit(2)),
                              cmds=SimpleNamespace(reset_chip=MagicMock(side_effect=RuntimeError('reset failed'))))
        args = SimpleNamespace(target='p4', port='/dev/test', baud=460800)
        with patch.dict(sys.modules, {'esptool': api}):
            with self.assertRaisesRegex(RuntimeError, 'exit 2'):
                flash.run_tool(args, 'flash_id')
            with self.assertRaisesRegex(RuntimeError, 'reset failed'):
                flash.run_tool(args, 'read_mac', reset=True)
        loader.connect.assert_called_once()
        loader._port.close.assert_called_once()

    def test_recovers_loader_left_at_high_baud(self):
        from types import SimpleNamespace
        from unittest.mock import MagicMock, call
        loader = MagicMock()
        loader.connect.side_effect = [RuntimeError('no reply at ROM baud'), None]
        loader._port.is_open = False
        api = SimpleNamespace(CHIP_DEFS={'esp32p4': MagicMock(return_value=loader)},
                              FatalError=RuntimeError, run_stub=MagicMock(return_value=loader),
                              main=MagicMock(), cmds=SimpleNamespace(reset_chip=MagicMock()))
        args = SimpleNamespace(target='p4', port='/dev/test', baud=460800)
        with patch.dict(sys.modules, {'esptool': api}):
            flash.run_tool(args, 'flash_id')
            flash.run_tool(args, 'read_mac', reset=True)
        self.assertEqual(loader.connect.call_args_list,
                         [call('default-reset'), call('no-reset', attempts=2)])
        self.assertEqual(loader._port.baudrate, 460800)
        loader.change_baud.assert_not_called()
        loader._port.open.assert_called_once()
        loader._port.close.assert_called_once()

    def test_confirmed_console_boot_skips_hardware_reset(self):
        from types import SimpleNamespace
        from unittest.mock import MagicMock
        self.console.return_value = True
        loader = MagicMock()
        api = SimpleNamespace(CHIP_DEFS={'esp32p4': MagicMock(return_value=loader)},
                              run_stub=MagicMock(return_value=loader),
                              main=MagicMock(), cmds=SimpleNamespace(reset_chip=MagicMock()))
        args = SimpleNamespace(target='p4', port='/dev/test', baud=460800)
        with patch.dict(sys.modules, {'esptool': api}):
            flash.run_tool(args, 'flash_id')
            flash.run_tool(args, 'read_mac', reset=True)
        loader.connect.assert_called_once_with('no-reset')
        self.console.assert_called_once_with('/dev/test')

    def test_timeout_restores_alarm_handler(self):
        import signal
        import time
        from flash_session import deadline
        previous = signal.getsignal(signal.SIGALRM)
        with self.assertRaises(TimeoutError):
            with deadline(0.01):
                time.sleep(0.1)
        self.assertEqual(signal.getsignal(signal.SIGALRM), previous)


class FullFlashTests(LayoutFixture, unittest.TestCase):
    def test_migration_preserves_nvs_and_phy(self):
        from full_flash import validate_migration
        new = self.gen.PartitionTable.from_csv(
            (flash.ROOT / 'firmware_p4/partitions_16mb.csv').read_text()).to_binary()
        validate_migration(bytes(self.data) + b'\xff' * 0x800000, new, self.gen)
        validate_migration(b'\xff' * 0x1000000, new, self.gen)
        moved = self.gen.PartitionTable.from_binary(new)
        next(p for p in moved if p.name == 'nvs').offset += 0x1000
        with self.assertRaisesRegex(ValueError, 'moves nvs'):
            validate_migration(bytes(self.data), moved.to_binary(), self.gen)

    def test_full_write_and_check_order(self):
        from types import SimpleNamespace
        for check in (True, False):
            with tempfile.TemporaryDirectory() as directory:
                dest = Path(directory)
                backup = dest / 'flash.bin'
                backup.write_bytes(self.data)
                args = SimpleNamespace(target='p4', check=check,
                                       full_images=[('app', 0x20000, b'new app')])
                report = {}
                with patch.object(flash, 'run_tool') as run:
                    flash.update_full(args, bytes(self.data), self.table, self.gen,
                                      dest, backup, report, dest / 'manifest.json')
                commands = [call.args[1] for call in run.call_args_list]
                self.assertEqual(commands, [] if check else
                                 ['verify_flash', 'write_flash', 'verify_flash'])
                self.assertEqual((dest / 'app.bin').read_bytes(), b'new app')
                self.assertEqual(report['status'], 'checked' if check else 'written_and_verified')

    def test_backup_mismatch_prevents_full_write(self):
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as directory:
            dest = Path(directory)
            args = SimpleNamespace(target='p4', check=False,
                                   full_images=[('app', 0x20000, b'app')])
            with patch.object(flash, 'run_tool', side_effect=RuntimeError('mismatch')) as run:
                with self.assertRaisesRegex(RuntimeError, 'mismatch'):
                    flash.update_full(args, bytes(self.data), self.table, self.gen,
                                      dest, dest / 'flash.bin', {}, dest / 'manifest.json')
            self.assertEqual([call.args[1] for call in run.call_args_list], ['verify_flash'])

    def test_bundle_rejects_missing_and_wrong_inputs(self):
        from full_flash import prepare
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            parts = {p.name: p for p in self.gen.PartitionTable.from_binary(self.table)}
            files = {'0x2000': 'bootloader/bootloader.bin',
                     '0x8000': 'partition_table/partition-table.bin',
                     '0xf000': 'ota_data_initial.bin', '0x20000': 'TentacleOS_P4.bin',
                     hex(parts['assets'].offset): 'assets.bin'}
            for name in files.values():
                path = build / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'app')
            (build / 'partition_table/partition-table.bin').write_bytes(self.table)
            (build / 'ota_data_initial.bin').write_bytes(b'\xff' * parts['otadata'].size)
            (build / 'assets.bin').write_bytes(b'A' * parts['assets'].size)
            (build / 'flasher_args.json').write_text(json.dumps({'flash_files': files}))
            image = SimpleNamespace(flash_size_freq=0x30,
                                    ROM_LOADER=SimpleNamespace(FLASH_SIZES={'8MB': 0x30}))
            validate = lambda *_: image
            def plan():
                return prepare(build, 'p4', self.flash_size, self.table, b'app', self.gen, validate)
            self.assertEqual(len(plan()), 6)
            image.flash_size_freq = 0x40
            with self.assertRaisesRegex(ValueError, 'Bootloader flash size'):
                plan()
            image.flash_size_freq = 0x30
            (build / 'assets.bin').unlink()
            with self.assertRaisesRegex(ValueError, 'Missing assets'):
                plan()
            files['0x20001'] = files.pop('0x20000')
            (build / 'flasher_args.json').write_text(json.dumps({'flash_files': files}))
            with self.assertRaisesRegex(ValueError, 'files/offsets'):
                plan()


class OtaSelectionTests(unittest.TestCase):
    def test_bootloader_ignores_only_timestamp(self):
        from types import SimpleNamespace
        from ota_flash import verify_bootloader
        payload = bytearray(96)
        payload[0] = 0x50
        def image(data, target):
            return SimpleNamespace(segments=[SimpleNamespace(addr=0x1000, data=data[24:])])
        api = SimpleNamespace(image_valid=image, require=flash.require)
        original = b'H' * 24 + bytes(payload)
        payload[40:64] = b'T' * 24
        verify_bootloader(original, b'H' * 24 + bytes(payload), api, 'p4')
        payload[64] = 1
        with self.assertRaisesRegex(ValueError, 'code/config differs'):
            verify_bootloader(original, b'H' * 24 + bytes(payload), api, 'p4')

    def test_alternating_selection_and_rollback(self):
        from ota_flash import selection, entry
        old = entry(1, 2)
        new = entry(2, 0)
        self.assertEqual(selection(old + b'\xff' * 4096), (0, 1, 0))
        with self.assertRaisesRegex(ValueError, 'pending'):
            selection(old + new)
        self.assertEqual(selection(old + entry(2, 2)), (1, 2, 1))
        self.assertEqual(selection(old + entry(2, 4)), (0, 1, 0))
        self.assertEqual(selection(entry(3, 2) + entry(2, 2)), (0, 3, 0))

    def test_unknown_state_bad_crc_and_wrap_are_rejected(self):
        from ota_flash import selection, entry
        for data in (entry(1, 7), entry(0xfffffffe, 2), b'\x00' * 4096):
            with self.assertRaises(ValueError):
                selection(data + b'\xff' * 4096)

    def test_session_switches_only_for_read_and_write(self):
        from types import SimpleNamespace
        from unittest.mock import MagicMock, call
        from flash_session import FlashSession
        args = SimpleNamespace(target='p4', baud=230400, read_baud=230400, write_baud=921600)
        session = FlashSession(args)
        session.ready = True
        session.esp = MagicMock()
        api = SimpleNamespace(main=MagicMock())
        with patch.dict(sys.modules, {'esptool': api}):
            session.run(['read_flash'], 2)
            session.run(['write_flash'], 2)
            session.run(['verify_flash'], 2)
            session.run(['read_flash'], 2)
        self.assertEqual(session.esp.change_baud.call_args_list, [call(921600), call(230400)])


if __name__ == '__main__':
    # Pass the SDK path without giving it to unittest's argument parser.
    sdk = sys.argv.pop()
    with patch.object(sys, 'argv', [sys.argv[0], sdk]):
        unittest.main(argv=[sys.argv[0]])
