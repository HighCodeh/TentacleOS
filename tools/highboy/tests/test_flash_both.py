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

"""Offline USB metadata and terminal-selection tests; never opens a device."""
import sys
import plistlib
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch, MagicMock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import flash_both
import flash_ports
import flash_session


class BothTests(unittest.TestCase):
    def run_pair(self, results, extra=()):
        with patch.object(flash_both.subprocess, 'run', side_effect=[SimpleNamespace(returncode=r) for r in results]) as run:
            code = flash_both.main(['--mode', 'app', '--p4-port', '/dev/p4', '--c5-port', '/dev/c5', *extra])
        return code, [call.args[0] for call in run.call_args_list]

    def test_preflight_then_c5_then_p4(self):
        code, calls = self.run_pair([0, 0, 0, 0])
        self.assertEqual(code, 0)
        self.assertEqual([command[2] for command in calls], ['c5', 'p4', 'c5', 'p4'])
        for command in calls[:2]: self.assertIn('--validate-build', command)
        for command in calls[2:]: self.assertNotIn('--validate-build', command)
        self.assertIn('--control-port', calls[2])
        self.assertEqual(calls[2][-1], '/dev/p4')

    def test_preflight_failure_never_updates(self):
        code, calls = self.run_pair([0, 2])
        self.assertEqual(code, 2)
        self.assertEqual(len(calls), 2)
        self.assertTrue(all('--validate-build' in cmd for cmd in calls))

    def test_c5_failure_stops_p4(self):
        code, calls = self.run_pair([0, 0, 2])
        self.assertEqual(code, 2)
        self.assertEqual(len(calls), 3)

    def test_p4_failure_reports_partial_update(self):
        code, calls = self.run_pair([0, 0, 0, 2])
        self.assertEqual(code, 2)
        self.assertEqual(len(calls), 4)

    def test_full_check_propagates_to_both(self):
        with patch.object(flash_both.subprocess, 'run', return_value=SimpleNamespace(returncode=0)) as run:
            self.assertEqual(flash_both.main(['--mode', 'full', '--check', '--p4-port', 'p4', '--c5-port', 'c5']), 0)
        for call in run.call_args_list:
            self.assertIn('full', call.args[0]); self.assertIn('--check', call.args[0])

    def test_auto_pair_and_ambiguous_pair(self):
        with patch.object(flash_both, 'resolve_port', return_value=['-p', '/dev/p4']), \
             patch.object(flash_both, 'peer_port', return_value=None), \
             patch.object(flash_both.subprocess, 'run') as run:
            self.assertEqual(flash_both.main(['--mode', 'app']), 2)
            run.assert_not_called()
        with patch.object(flash_both.subprocess, 'run') as run:
            self.assertEqual(flash_both.main(['--mode', 'app', '--p4-port', 'same', '--c5-port', 'same']), 2)
            run.assert_not_called()

    def test_peer_is_same_board(self):
        def device(name, loc):
            return SimpleNamespace(device=name, location=loc, serial_number=None, vid=0x10c4, pid=0xea70, interface='', hwid='')
        inventory=[device('p4', '1-2:1.0'), device('c5', '1-2:1.1'), device('other', '1-3:1.1')]
        ports=SimpleNamespace(comports=lambda: inventory)
        with patch.dict(sys.modules, {'serial': MagicMock(), 'serial.tools': SimpleNamespace(list_ports=ports)}), \
             patch.object(flash_ports.sys, 'platform', 'linux'):
            self.assertEqual(flash_ports.peer_port('p4', 'c5'), 'c5')
            inventory.pop(1)
            self.assertIsNone(flash_ports.peer_port('p4', 'c5'))

    def test_mac_peer_uses_physical_device_not_serial_driver(self):
        def board(name, location):
            interfaces = []
            for index, target in enumerate(('p4', 'c5')):
                interfaces.append({
                    'IOObjectClass': 'IOUSBHostInterface', 'idVendor': 0x10c4,
                    'idProduct': 0xea70, 'locationID': location, 'bInterfaceNumber': index,
                    'IORegistryEntryChildren': [{
                        'IOObjectClass': 'IOUserSerial', 'idVendor': 0x10c4,
                        'idProduct': 0xea70, 'bInterfaceNumber': index,
                        'IORegistryEntryID': location + index + 1,
                        'IORegistryEntryChildren': [{'IOCalloutDevice': name + target}],
                    }],
                })
            return {'IOObjectClass': 'IOUSBHostDevice', 'idVendor': 0x10c4,
                    'idProduct': 0xea70, 'locationID': location,
                    'IORegistryEntryChildren': interfaces}

        nodes = [board('first-', 100), board('second-', 200)]
        inventory = [SimpleNamespace(device=name + target, location=None, serial_number=None,
                                    vid=0x10c4, pid=0xea70, interface=None, hwid='')
                     for name in ('first-', 'second-') for target in ('p4', 'c5')]
        ports = SimpleNamespace(comports=lambda: inventory)
        with patch.dict(sys.modules, {'serial': MagicMock(), 'serial.tools': SimpleNamespace(list_ports=ports)}), \
             patch.object(flash_ports.sys, 'platform', 'darwin'), \
             patch.object(flash_ports.subprocess, 'run', return_value=SimpleNamespace(stdout=plistlib.dumps(nodes))):
            self.assertEqual(flash_ports.peer_port('first-p4', 'c5'), 'first-c5')
            self.assertEqual(flash_ports.peer_port('first-c5', 'p4'), 'first-p4')
            self.assertEqual(flash_ports.peer_port('second-p4', 'c5'), 'second-c5')
            inventory.pop(1)
            self.assertIsNone(flash_ports.peer_port('first-p4', 'c5'))

    def test_c5_console_boot_and_watchdog(self):
        loader=MagicMock(); loader._port.baudrate=115200
        api=SimpleNamespace(CHIP_DEFS={'esp32c5': MagicMock(return_value=loader)},
                            run_stub=MagicMock(return_value=loader), cmds=SimpleNamespace(reset_chip=MagicMock()))
        args=SimpleNamespace(target='c5', port='c5', control_port='p4', before='default-reset', baud=115200)
        with patch.dict(sys.modules, {'esptool':api}), patch.object(flash_session, 'request_console_boot', return_value=True) as boot:
            session=flash_session.FlashSession(args);session.connect();session.close(2)
            boot.assert_called_once_with('p4', 'c5')
        loader.connect.assert_called_once_with('no-reset')
        api.cmds.reset_chip.assert_called_once_with(loader, 'watchdog-reset')

    def test_console_requires_success_ack(self):
        connection=MagicMock()
        connection.read.side_effect=[b'highboy>', b'C5 ROM boot: ESP_OK']
        serial=SimpleNamespace(Serial=MagicMock(return_value=connection), SerialException=OSError)
        with patch.dict(sys.modules, {'serial':serial}), patch.object(flash_session.time, 'sleep'):
            self.assertTrue(flash_session.request_console_boot('p4', 'c5'))
        self.assertIn(((b'c5 boot\r\n',), {}), connection.write.call_args_list)
        connection.close.assert_called_once()

    def test_console_answers_fragmented_cursor_queries_before_command(self):
        connection = MagicMock()
        connection.read.side_effect = [b'highboy>\x1b[', b'6n',
                                       b'\x1b[999C\x1b[6n', b'\x1b[79Dhighboy>',
                                       b'C5 ROM boot: ESP_OK']
        serial = SimpleNamespace(Serial=MagicMock(return_value=connection), SerialException=OSError)
        with patch.dict(sys.modules, {'serial': serial}), patch.object(flash_session.time, 'sleep'):
            self.assertTrue(flash_session.request_console_boot('p4', 'c5'))
        self.assertEqual([call.args[0] for call in connection.write.call_args_list],
                         [b'\x15\r\n', b'\x1b[1;1R', b'\x1b[1;80R', b'c5 boot\r\n'])

    def test_console_recovers_query_left_by_previous_monitor(self):
        connection = MagicMock()
        connection.read.side_effect = [b'', b'highboy>', b'C5 ROM boot: ESP_OK']
        serial = SimpleNamespace(Serial=MagicMock(return_value=connection), SerialException=OSError)
        with patch.dict(sys.modules, {'serial': serial}), \
             patch.object(flash_session.time, 'sleep'), \
             patch.object(flash_session.time, 'monotonic', side_effect=range(20)):
            self.assertTrue(flash_session.request_console_boot('p4', 'c5'))
        self.assertEqual([call.args[0] for call in connection.write.call_args_list],
                         [b'\x15\r\n', b'\x1b[1;1R\x15\r', b'c5 boot\r\n'])

    def test_c5_link_confirmation_required_before_next_target(self):
        with patch.object(flash_session, 'request_console_command', return_value=True) as request, \
             patch.object(flash_session.time, 'sleep') as settle:
            flash_session.confirm_c5_boot('p4')
            request.assert_called_once_with('p4', 'c5 wait 10', b'C5 link: ALIVE')
            settle.assert_called_once_with(flash_session.BOOT_SETTLE_SECONDS)
        with patch.object(flash_session, 'request_console_command', return_value=False), \
             patch.object(flash_session.time, 'sleep') as settle:
            with self.assertRaisesRegex(RuntimeError, 'confirmation failed'):
                flash_session.confirm_c5_boot('p4')
            settle.assert_not_called()


if __name__ == '__main__':
    unittest.main()
