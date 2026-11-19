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

import contextlib
import io
from pathlib import Path
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import scope

IDLE = 'RF frontend: idle\nLF: field=OFF generated=125000 Hz EN(GPIO2)=0 MOD(GPIO11)=0 CL=0 DA=0\n'
FIELD = ('LF field: NO CARRIER FEEDBACK, source=external/passive reference=125000 Hz '
         'generated=0 Hz measured=0 Hz\nCL rises=0; DA edges=0 (0 edges/s)\n')


class FakeLink:
    def __init__(self):
        self.commands = []
        self.closed = False
        self.replies = {
            'rfbench info': 'INFO schema=1 version=5af4fea elf_sha256=' + 'a' * 64,
            'rfid status': IDLE,
            'rfid off': 'LF field OFF',
            'rfid listen 100': FIELD,
            'rfid listen 1000': FIELD.replace('NO CARRIER FEEDBACK', 'LOCKED').replace('measured=0', 'measured=125000'),
            'rfid emutest 5000 0': 'EM4100 waveform complete: outputs OFF',
            'rfid field 1000': FIELD,
        }
    def console(self):
        pass
    def command(self, command, timeout=5):
        self.commands.append(command)
        value = self.replies[command]
        if isinstance(value, Exception):
            raise value
        return value
    def close(self):
        self.closed = True


class FakeFlipper:
    def __init__(self):
        self.active = False
        self.events = []
        self.started = False
        self.stopped = False
        self.closed = False
        self.natural_stop = False
        self.u = SimpleNamespace(close=self.close)
    def send(self, data):
        pass
    def receive(self, seconds):
        text = 'No application is running\n>: '
        if self.active:
            text = 'EM4100 0102030405\nReading stopped\n>: ' if self.natural_stop else ''
        self.events.append(dict(kind='rx', value=text))
        return text
    def start(self, reader=False):
        self.started = self.active = True
    def stop(self):
        if self.active:
            self.stopped = True
            self.active = False
    def close(self):
        self.closed = True


class Tests(unittest.TestCase):
    def setUp(self):
        self.link = FakeLink()
        self.flipper = FakeFlipper()
        self.cancelled = threading.Event()
        self.devices = [dict(device='flipper', vid=0x0483, pid=0x5740)]
    def run_action(self, action='emular'):
        with tempfile.TemporaryDirectory() as tmp, \
             patch.object(scope.lab, 'Link', return_value=self.link), \
             patch.object(scope.pair, 'Flipper', return_value=self.flipper), \
             patch.object(scope.lab, 'ports', side_effect=lambda: self.devices), \
             contextlib.redirect_stdout(io.StringIO()):
            args = SimpleNamespace(highboy='p4', flipper='flipper', output=Path(tmp))
            report = scope.run_action(args, action, self.cancelled)
            self.assertTrue((Path(report['output'])/'report.json').exists())
            return report
    def test_emulation_does_not_claim_decode_and_cleans_both(self):
        report = self.run_action()
        self.assertEqual(report['status'], 'complete')
        self.assertFalse(report['test_id_decoded'])
        self.assertTrue(report['highboy_off'] and report['flipper_off'])
        self.assertTrue(self.flipper.started and self.flipper.stopped)
        self.assertTrue(self.link.closed and self.flipper.closed)
    def test_natural_decode_is_captured_without_second_stop(self):
        self.flipper.natural_stop = True
        report = self.run_action()
        self.assertTrue(report['test_id_decoded'])
        self.assertFalse(self.flipper.stopped)
        self.assertFalse(self.flipper.active)
    def test_failed_command_still_stops_flipper(self):
        self.link.replies['rfid emutest 5000 0'] = RuntimeError('USB gone')
        report = self.run_action()
        self.assertEqual(report['status'], 'failed')
        self.assertTrue(self.flipper.stopped and self.flipper.closed)
        self.assertIn('rfid off', self.link.commands)
    def test_cleanup_failure_is_not_success(self):
        self.link.replies['rfid off'] = RuntimeError('USB gone')
        report = self.run_action()
        self.assertEqual(report['status'], 'failed')
        self.assertTrue(report['cleanup_errors'])
        self.assertTrue(self.flipper.stopped)
    def test_cancel_before_activation(self):
        self.cancelled.set()
        report = self.run_action()
        self.assertEqual(report['status'], 'cancelled')
        self.assertNotIn('rfid emutest 5000 0', self.link.commands)
        self.assertFalse(self.flipper.started)
    def test_wrong_firmware_never_starts_or_sends_off(self):
        self.link.replies['rfbench info'] = 'unknown'
        report = self.run_action()
        self.assertEqual(report['status'], 'failed')
        self.assertNotIn('rfid off', self.link.commands)
        self.assertFalse(self.flipper.started)
    def test_external_field_inhibits_highboy_carrier(self):
        self.link.replies['rfid listen 100'] = FIELD.replace('NO CARRIER FEEDBACK', 'LOCKED')
        report = self.run_action('campo')
        self.assertEqual(report['status'], 'failed')
        self.assertNotIn('rfid field 1000', self.link.commands)
    def test_missing_reader_field_prevents_emulation_and_stops_reader(self):
        self.link.replies['rfid listen 1000'] = FIELD
        report = self.run_action()
        self.assertEqual(report['status'], 'failed')
        self.assertNotIn('rfid emutest 5000 0', self.link.commands)
        self.assertTrue(self.flipper.stopped)
    def test_status_does_not_start_flipper(self):
        report = self.run_action('status')
        self.assertEqual(report['status'], 'complete')
        self.assertFalse(self.flipper.started)
    def test_field_works_without_flipper_usb(self):
        self.devices = []
        report = self.run_action('campo')
        self.assertEqual(report['status'], 'complete')
        self.assertIn('rfid field 1000', self.link.commands)
        self.assertFalse(self.flipper.started)
    def test_emulation_accepts_manual_external_reader_without_usb(self):
        self.devices = []
        report = self.run_action()
        self.assertEqual(report['status'], 'complete')
        self.assertFalse(self.flipper.started)
        self.assertNotIn('test_id_decoded', report)
    def test_no_reader_refuses_modulation_without_usb_error(self):
        self.devices = []
        self.link.replies['rfid listen 1000'] = FIELD
        report = self.run_action()
        self.assertIn('Campo externo de 125 kHz não confirmado', report['error'])
        self.assertNotIn('rfid emutest 5000 0', self.link.commands)
    def test_busy_frontend_is_not_stopped(self):
        self.link.replies['rfid status'] = IDLE.replace('idle', 'NFC')
        self.run_action()
        self.assertNotIn('rfid off', self.link.commands)
        self.assertFalse(self.flipper.started)


if __name__ == '__main__':
    unittest.main()
