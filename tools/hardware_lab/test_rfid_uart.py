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

"""Host tests of the actual standalone UART parser and edge-based EM4100 decoder."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "firmware_lab_rfid"
DRIVER = PROJECT / "components/Drivers/rfid_lab_io"
SERVICE = PROJECT / "components/Service/rfid_lab"

class Command(C.Structure):
    _fields_ = [("kind", C.c_int)] + [(name, C.c_uint32) for name in
        ("hz", "end_hz", "step_hz", "duty", "ms", "pin", "level", "invert")]

class Lane(C.Structure):
    _fields_ = [("shift", C.c_uint64), ("candidate", C.c_uint64),
        ("bits", C.c_uint32), ("last_match", C.c_uint32),
        ("first_level", C.c_uint8), ("repeats", C.c_uint8), ("has_first", C.c_bool)]

class Decoder(C.Structure):
    _fields_ = [("lanes", Lane * 2), ("halves", C.c_uint32),
        ("invalid_pulses", C.c_uint32), ("id", C.c_uint64), ("is_valid", C.c_bool)]

class RFIDUARTTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        library = Path(cls.temp.name) / "rfid.dylib"
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
            "-I" + str(DRIVER / "include"), "-I" + str(SERVICE / "include"),
            "-I" + str(ROOT / "firmware_p4/components/Drivers/pins/include"),
            str(DRIVER / "rfid_lab_protocol.c"), str(SERVICE / "rfid_lab_command.c"),
            "-o", str(library)], check=True)
        cls.lib = C.CDLL(str(library))
        cls.lib.rfid_lab_protocol_encode.argtypes = [C.c_uint64]
        cls.lib.rfid_lab_protocol_encode.restype = C.c_uint64
        cls.lib.rfid_lab_protocol_validate.argtypes = [C.c_uint64, C.POINTER(C.c_uint64)]
        cls.lib.rfid_lab_protocol_validate.restype = C.c_bool
        cls.lib.rfid_lab_protocol_reset.argtypes = [C.POINTER(Decoder)]
        cls.lib.rfid_lab_protocol_feed.argtypes = [C.POINTER(Decoder), C.c_uint32, C.c_bool]
        cls.lib.rfid_lab_command_parse.argtypes = [C.c_char_p, C.POINTER(Command)]
        cls.lib.rfid_lab_command_parse.restype = C.c_bool

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def parse(self, text):
        command = Command()
        ok = self.lib.rfid_lab_command_parse(C.create_string_buffer(text.encode()), C.byref(command))
        return ok, command

    def pulses(self, frame, repeats=6, invert=0, half=256, offset=0):
        levels = []
        for _ in range(repeats):
            for bit in range(63, -1, -1):
                level = ((frame >> bit) & 1) ^ invert
                levels.extend((level, 1 - level))
        levels = levels[offset:]
        result = []
        for level in levels:
            if result and result[-1][1] == level:
                result[-1] = (result[-1][0] + half, level)
            else:
                result.append((half, level))
        return result

    def decode(self, pulses):
        decoder = Decoder()
        self.lib.rfid_lab_protocol_reset(C.byref(decoder))
        for duration, level in pulses:
            self.lib.rfid_lab_protocol_feed(C.byref(decoder), duration, level)
        return decoder

    def test_commands_and_sweep_bounds(self):
        for text in ("help", "?", "info", "status", "off", "pins", "reboot", "romboot ARMED",
                     "field", "pulse", "read", "raw", "listen", "emulate", "sweep",
                     "field 20 10 20", "field 200 90 5000", "pulse 1 50 1000",
                     "gpio 2 1 100", "gpio 11 1 1000", "gpio 2 0", "emulate 5000 1",
                     "sweep 20 200 10 5000", "sweep 120 130 1 1000", "campo", "parar"):
            with self.subTest(text=text): self.assertTrue(self.parse(text)[0])
        ok, command = self.parse("sweep")
        self.assertTrue(ok)
        self.assertEqual((command.hz, command.end_hz, command.step_hz, command.ms),
                         (20000, 200000, 10000, 5000))
        self.assertEqual((command.end_hz - command.hz) // command.step_hz + 1, 19)

    def test_reject_unsafe_or_ambiguous_commands(self):
        for text in ("", "rfid on", "gpio 15 1", "gpio 16 1", "gpio 37 1", "gpio 11 2",
                     "gpio 2 1 1001", "gpio 2", "field 0", "field 19", "field 201",
                     "field 125 0", "field 125 100", "field 125 50 0", "field 125 50 5001",
                     "pulse 0", "pulse 10001", "emulate 1000 2", "sweep 20 200 0 5000",
                     "sweep 20 200 10 6000", "sweep 20 200 1 5000", "sweep 200 20 10 5000",
                     "sweep 20 200 11 1000", "sweep 20", "off anything", "romboot", "romboot armed",
                     "field -125", "field +125", "field 125abc", "field 0x7d", "field 4294967296",
                     "field 99999999999999999999999999999", "help a b c d e f g"):
            with self.subTest(text=text): self.assertFalse(self.parse(text)[0])

    def test_frame_parity_and_known_encoding(self):
        # Independent row/column reference, not the firmware encoder.
        def reference(uid):
            bits = [1] * 9
            columns = [0] * 4
            for ch in f"{uid:010x}":
                row = [(int(ch, 16) >> shift) & 1 for shift in (3, 2, 1, 0)]
                bits += row + [sum(row) % 2]
                columns = [a ^ b for a, b in zip(columns, row)]
            bits += columns + [0]
            return int("".join(map(str, bits)), 2)
        for uid in (0, 0x0102030405, 0x5e0028cd0a, 0xffffffffff, 0x123456789a):
            frame = self.lib.rfid_lab_protocol_encode(uid)
            self.assertEqual(frame, reference(uid))
            decoded = C.c_uint64()
            self.assertTrue(self.lib.rfid_lab_protocol_validate(frame, C.byref(decoded)))
            self.assertEqual(decoded.value, uid)
            for bit in range(64):
                self.assertFalse(self.lib.rfid_lab_protocol_validate(frame ^ (1 << bit), C.byref(decoded)))

    def test_decode_polarity_phase_and_clock_tolerance(self):
        for uid in (0x0102030405, 0x5e0028cd0a, 0xffffffffff, 0):
            frame = self.lib.rfid_lab_protocol_encode(uid)
            for invert in (0, 1):
                for half in (248, 253, 256, 259, 264):
                    for offset in (0, 1, 7, 31, 100):
                        with self.subTest(uid=uid, invert=invert, half=half, offset=offset):
                            result = self.decode(self.pulses(frame, invert=invert, half=half, offset=offset))
                            self.assertTrue(result.is_valid)
                            self.assertEqual(result.id, uid)

    def test_invalid_data_and_short_sequences(self):
        frame = self.lib.rfid_lab_protocol_encode(0x0102030405)
        self.assertFalse(self.decode(self.pulses(frame, repeats=2)).is_valid)
        self.assertFalse(self.decode(self.pulses(frame ^ (1 << 12))).is_valid)
        self.assertFalse(self.decode([(8, i % 2) for i in range(5000)]).is_valid)
        self.assertFalse(self.decode([(256, 0)] * 1000).is_valid)
        self.assertFalse(self.decode(self.pulses(frame, half=64)).is_valid)
        self.assertFalse(self.decode(self.pulses(frame, half=128)).is_valid)

    def test_resync_after_gap_and_noisy_start(self):
        uid = 0x5e0028cd0a
        frame = self.lib.rfid_lab_protocol_encode(uid)
        prefix = self.pulses(frame, repeats=1) + [(10000, 0), (7, 1), (900, 0)]
        result = self.decode(prefix + self.pulses(frame))
        self.assertTrue(result.is_valid)
        self.assertEqual(result.id, uid)

if __name__ == "__main__": unittest.main()
