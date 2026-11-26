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
import io
import os
from pathlib import Path
import pty
import plistlib
import select
import subprocess
import sys
import time
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import flash_ports as ports


def device(**kwargs):
    values = dict(vid=0x10C4, pid=0xEA70, device='/dev/usb', interface=None,
                  location=None, hwid='', serial_number='unit')
    values.update(kwargs)
    return SimpleNamespace(**values)


class PortTests(unittest.TestCase):
    def test_explicit_modes_skip_menu(self):
        for value in ('app', 'full'):
            with mock.patch.object(ports, 'choose') as choose:
                args = ['p4', '--mode', value]
                self.assertEqual(ports.resolve_mode(args), args)
                choose.assert_not_called()
        with self.assertRaises(ValueError):
            ports.resolve_mode(['p4', '--mode', 'invalid'])

    def test_full_mode_menu(self):
        with mock.patch.object(ports.sys.stdin, 'isatty', return_value=True), \
             mock.patch.object(ports.sys.stderr, 'isatty', return_value=True), \
             mock.patch.object(ports, 'choose', return_value=1):
            self.assertEqual(ports.resolve_mode(['p4']), ['p4', '--mode', 'full'])

    def test_interface_metadata(self):
        for field, value, target in [('interface', 'Enhanced Com Port', 'p4'),
                                     ('interface', 'SCI', 'c5'),
                                     ('location', '2-3:1.0', 'p4'),
                                     ('location', '2-3:1.1', 'c5'),
                                     ('hwid', 'USB VID_10C4&PID_EA70&MI_01', 'c5')]:
            self.assertEqual(ports.port_target(device(**{field: value}), {}), target)

    def test_no_filename_guess_or_conflicting_evidence(self):
        self.assertIsNone(ports.port_target(device(device='/dev/cu.usbserial-020189170'), {}))
        self.assertIsNone(ports.port_target(device(interface='ECI', location='2-3:1.1'), {}))
        self.assertIsNone(ports.port_target(device(vid=0x303A, interface='ECI'), {}))

    def test_macos_registry_ancestry(self):
        tree = [{'idVendor': 0x10C4, 'idProduct': 0xEA70, 'IORegistryEntryChildren': [
            {'bInterfaceNumber': number, 'IORegistryEntryChildren': [
                {'IORegistryEntryChildren': [{'IOCalloutDevice': f'/dev/cu.{number}'}]}]}
            for number in (0, 1)]}]
        mapping = ports.registry_interfaces(tree)
        self.assertEqual(mapping, {'/dev/cu.0': 'p4', '/dev/cu.1': 'c5'})
        self.assertEqual(ports.port_target(device(device='/dev/cu.1'), mapping), 'c5')

    def test_macos_query_requests_descendant_properties(self):
        tree = [{'idVendor': 0x10C4, 'idProduct': 0xEA70,
                 'IORegistryEntryChildren': [{
                     'bInterfaceNumber': 0,
                     'IORegistryEntryChildren': [{'IOCalloutDevice': '/dev/cu.p4'}]}]}]
        def ioreg(command, **kwargs):
            # Without -l, ioreg omits properties of descendants not matching -c.
            data = tree if '-l' in command else [{'idVendor': 0x10C4, 'idProduct': 0xEA70}]
            return SimpleNamespace(stdout=plistlib.dumps(data))
        with mock.patch.object(ports.sys, 'platform', 'darwin'):
            with mock.patch.object(ports.subprocess, 'run', side_effect=ioreg):
                self.assertEqual(ports.mac_interfaces(), {'/dev/cu.p4': 'p4'})

    def test_manual_never_enumerates(self):
        with mock.patch.object(ports, 'discover') as discover:
            for option in (['-p', '/dev/manual'], ['--port=/dev/manual'], ['-p/dev/manual']):
                args = ['--target', 'p4', *option]
                self.assertEqual(ports.resolve_port(args), args)
            discover.assert_not_called()

    def test_one_match_automatic(self):
        with mock.patch.object(ports, 'discover', return_value=[device()]) as discover:
            self.assertEqual(ports.resolve_port(['--target', 'c5']),
                             ['--target', 'c5', '-p', '/dev/usb'])
            discover.assert_called_once_with('c5')

    def test_multiple_and_no_match(self):
        with mock.patch.object(ports, 'discover', return_value=[]):
            with self.assertRaisesRegex(ValueError, 'No identifiable'):
                ports.resolve_port(['--target', 'p4'])
        with mock.patch.object(ports, 'discover', return_value=[device(device='/dev/a'), device(device='/dev/b')]):
            with mock.patch.object(ports, 'choose', return_value=1):
                self.assertEqual(ports.resolve_port(['--target', 'p4'])[-1], '/dev/b')
            with self.assertRaisesRegex(ValueError, 'terminal'):
                ports.resolve_port(['--target', 'p4'])

    def test_noninteractive_menu_fails(self):
        with self.assertRaisesRegex(ValueError, 'terminal'):
            ports.choose('Chip', ['P4', 'C5'], io.StringIO(), io.StringIO())

    def test_real_arrow_menu_and_escape(self):
        for keys, expected, status in [(b'\x1b[B\r', b'c5', 0),
                                       (b'\r', b'p4', 0), (b'\x1b[B\x1b[B\r', b'both', 0), (b'\x1b', b'Cancelled', 130)]:
            master, slave = pty.openpty()
            import termios
            original = termios.tcgetattr(slave)
            child = subprocess.Popen([sys.executable, ports.__file__, '--select-target'],
                                     stdin=slave, stdout=slave, stderr=slave)
            data = b''
            try:
                deadline = time.monotonic() + 5
                while b'ESP32-C5' not in data and time.monotonic() < deadline:
                    if select.select([master], [], [], 0.1)[0]:
                        data += os.read(master, 4096)
                self.assertIn(b'ESP32-C5', data)
                os.write(master, keys)
                deadline = time.monotonic() + 5
                while child.poll() is None and time.monotonic() < deadline:
                    if select.select([master], [], [], 0.05)[0]:
                        data += os.read(master, 4096)
                self.assertEqual(child.poll(), status, repr(data))
                while select.select([master], [], [], 0.1)[0]:
                    data += os.read(master, 4096)
                self.assertIn(expected, data)
                restored = termios.tcgetattr(slave)
                # macOS may set PENDIN when input arrives across a termios change.
                restored[3] &= ~getattr(termios, 'PENDIN', 0)
                original[3] &= ~getattr(termios, 'PENDIN', 0)
                self.assertEqual(restored, original)
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait()
                os.close(master)
                os.close(slave)


if __name__ == '__main__':
    unittest.main()
