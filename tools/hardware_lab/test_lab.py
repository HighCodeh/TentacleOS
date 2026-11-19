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

import struct
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path
import threading

import lab

FIELD = ('LF field: NO CARRIER FEEDBACK, source=High Boy reference=125000 Hz '
         'generated=125000 Hz measured=0 Hz\nCL rises=0; DA edges=0 (0 edges/s)\n')
IDLE = 'RF frontend: idle\nLF: field=OFF generated=125000 Hz EN(GPIO2)=0 MOD(GPIO11)=0 CL=0 DA=0\n'

class FakeLink:
    def __init__(self, replies):
        self.replies = replies
        self.commands = []
    def command(self, command, timeout=5):
        self.commands.append(command)
        value = self.replies[command]
        if isinstance(value, Exception):
            raise value
        return value

class Tests(unittest.TestCase):
    def test_missing_measurement_is_not_pass(self):
        with self.assertRaises(RuntimeError):
            lab.inspect('lf-field', 'Unrecognized command')
    def test_no_carrier_is_not_observed(self):
        self.assertEqual(lab.inspect('lf-field', FIELD)['result'], 'not_detected')
    def test_frequency_pass_without_carrier_is_not_measurement(self):
        row = lab.inspect('lf-selftest', 'LF self-test: GPIO static=PASS carrier=FAIL frequency=PASS result=FAIL\n' + FIELD)
        self.assertFalse(row['frequency_measured'])
    def test_zero_reads_is_not_pass(self):
        self.assertEqual(lab.inspect('lf-read', 'RFID live reader finished: attempts=33 valid=0')['result'], 'not_detected')
    def test_valid_tag_and_count(self):
        row = lab.inspect('lf-read', 'EM4100 LIVE: 0102030405 decimal=4328719365\nRFID live reader finished: attempts=33 valid=5')
        self.assertEqual(row['tags'], ['0102030405'])
        self.assertEqual(row['valid_frames'], 5)
    def test_read_count_without_tag_is_rejected(self):
        with self.assertRaises(RuntimeError):
            lab.inspect('lf-read', 'RFID live reader finished: attempts=33 valid=5')
    def test_console_cannot_be_labelled_c5(self):
        with self.assertRaises(ValueError):
            lab.Runner().run('lf-field', '/not/a/port', chip='c5')
    def test_truncated_edges_rejected(self):
        with self.assertRaises(RuntimeError):
            lab.inspect('lf-raw', 'DA edges captured=2 (delta_us,level): 256,1')
    def test_partial_sweep_rejected(self):
        with self.assertRaises(RuntimeError):
            lab.inspect('lf-sweep', '118000,117994,0,0,0,0')
    def test_complete_sweep_has_real_frequency_column(self):
        text = '\n'.join(f'{hz},{hz},{hz},0,100,1' for hz in range(118000,133000,1000))
        self.assertEqual(len(lab.inspect('lf-sweep', text)['rows']), 15)
    def test_active_or_other_frontend_rejected(self):
        for text in (IDLE.replace('idle', 'NFC 13.56 MHz'), IDLE.replace('field=OFF', 'field=ON')):
            self.assertFalse(lab.idle(text))
    def test_external_field_prevents_own_field(self):
        link = FakeLink({'rfid status': IDLE, 'rfid listen 100': FIELD.replace('NO CARRIER FEEDBACK','LOCKED')})
        with self.assertRaises(RuntimeError):
            lab.Runner().console_test(link, 'lf-field')
        self.assertNotIn('rfid field 1000', link.commands)
    def test_cleanup_runs_after_command_failure(self):
        link = FakeLink({'rfid status': IDLE, 'rfid listen 100': FIELD,
                         'rfid field 1000': RuntimeError('disconnected'), 'rfid off': 'LF field OFF'})
        with self.assertRaises(RuntimeError):
            lab.Runner().console_test(link, 'lf-field')
        self.assertEqual(link.commands[-2:], ['rfid off', 'rfid status'])
    def test_stop_before_activation(self):
        link = FakeLink({'rfid status': IDLE, 'rfid listen 100': FIELD})
        runner = lab.Runner();runner.cancelled.set()
        with self.assertRaises(lab.Cancelled):
            runner.console_test(link, 'lf-field')
        self.assertNotIn('rfid field 1000', link.commands)
    def test_unconfirmed_off_fails(self):
        link = FakeLink({'rfid status': IDLE, 'rfid listen 100': FIELD,
                         'rfid field 1000': FIELD, 'rfid off': RuntimeError('USB lost')})
        with self.assertRaises(RuntimeError):
            lab.Runner().console_test(link, 'lf-field')
    def test_missing_external_field_is_explicit(self):
        row = lab.inspect('lf-modulation', 'rfid: T2 modulation-path test refused/failed: ESP_ERR_NOT_FOUND')
        self.assertEqual(row['result'], 'not_detected')
    def test_sync_ack_checks_direction_length_and_status(self):
        packet = struct.pack('<BBHI', 1, 8, 4, 0) + bytes(4)
        self.assertTrue(lab.sync_ok(lab.slip(packet)))
        for packet in (b'\x00'+packet[1:], packet[:-1], packet[:8]+b'\x01\x00\x00\x00'):
            self.assertFalse(lab.sync_ok(lab.slip(packet)))
        self.assertFalse(lab.sync_ok(b'waiting for download'))
    def test_usb_wrong_chip_and_stale_app_do_not_pass(self):
        row = lab.boot_observation(b'ESP-ROM:esp32c5-eco3\nboot:0x18 (SPI_FAST_FLASH_BOOT)\nhighboy>', 'p4')
        self.assertFalse(row['chip_confirmed'])
        self.assertFalse(lab.boot_observation(b'highboy>', 'p4')['flash_boot'])
    def test_port_failure_still_saves_report(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(lab, 'Link', side_effect=RuntimeError('no USB')):
            runner = lab.Runner(tmp)
            result = runner.run('baseline', '/not/a/port')
            self.assertEqual(result['status'], 'failed')
            self.assertTrue((Path(result['output'])/'report.json').exists())
            self.assertFalse(runner.lock.locked())
    def test_duplicate_run_rejected(self):
        runner = lab.Runner();runner.lock.acquire()
        try:
            with self.assertRaises(RuntimeError):
                runner.run('baseline', '/not/a/port')
        finally:
            runner.lock.release()

if __name__ == '__main__':
    unittest.main()
