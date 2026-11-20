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

"""Clock, parity, polarity and resynchronization vectors for the full firmware decoder."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / "firmware_p4"
DRIVER = PROJECT / "components/Drivers/lf_rfid"

class Lane(C.Structure):
    _fields_ = [("shift", C.c_uint64), ("candidate", C.c_uint64),
        ("bits", C.c_uint32), ("last_match", C.c_uint32),
        ("first_level", C.c_uint8), ("repeats", C.c_uint8), ("has_first", C.c_bool)]

class Decoder(C.Structure):
    _fields_ = [("lanes", Lane * 2), ("halves", C.c_uint32),
        ("invalid_pulses", C.c_uint32), ("id", C.c_uint64), ("is_valid", C.c_bool), ("inverted", C.c_bool)]

class RFIDUARTTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        library = Path(cls.temp.name) / "rfid.dylib"
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
            "-I" + str(DRIVER / "include"),
            "-I" + str(ROOT / "firmware_p4/components/Drivers/pins/include"),
            str(DRIVER / "lf_rfid_decoder.c"),
            "-o", str(library)], check=True)
        cls.lib = C.CDLL(str(library))
        cls.lib.lf_rfid_decoder_encode.argtypes = [C.c_uint64]
        cls.lib.lf_rfid_decoder_encode.restype = C.c_uint64
        cls.lib.lf_rfid_decoder_validate.argtypes = [C.c_uint64, C.POINTER(C.c_uint64)]
        cls.lib.lf_rfid_decoder_validate.restype = C.c_bool
        cls.lib.lf_rfid_decoder_reset.argtypes = [C.POINTER(Decoder)]
        cls.lib.lf_rfid_decoder_feed.argtypes = [C.POINTER(Decoder), C.c_uint32, C.c_bool]

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

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
        self.lib.lf_rfid_decoder_reset(C.byref(decoder))
        for duration, level in pulses:
            self.lib.lf_rfid_decoder_feed(C.byref(decoder), duration, level)
        return decoder

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
            frame = self.lib.lf_rfid_decoder_encode(uid)
            self.assertEqual(frame, reference(uid))
            decoded = C.c_uint64()
            self.assertTrue(self.lib.lf_rfid_decoder_validate(frame, C.byref(decoded)))
            self.assertEqual(decoded.value, uid)
            for bit in range(64):
                self.assertFalse(self.lib.lf_rfid_decoder_validate(frame ^ (1 << bit), C.byref(decoded)))

    def test_decode_polarity_phase_and_clock_tolerance(self):
        for uid in (0x0102030405, 0x5e0028cd0a, 0xffffffffff, 0):
            frame = self.lib.lf_rfid_decoder_encode(uid)
            for invert in (0, 1):
                for half in (248, 253, 256, 259, 264):
                    for offset in (0, 1, 7, 31, 100):
                        with self.subTest(uid=uid, invert=invert, half=half, offset=offset):
                            result = self.decode(self.pulses(frame, invert=invert, half=half, offset=offset))
                            self.assertTrue(result.is_valid)
                            self.assertEqual(result.id, uid)

    def test_invalid_data_and_short_sequences(self):
        frame = self.lib.lf_rfid_decoder_encode(0x0102030405)
        self.assertFalse(self.decode(self.pulses(frame, repeats=2)).is_valid)
        self.assertFalse(self.decode(self.pulses(frame ^ (1 << 12))).is_valid)
        self.assertFalse(self.decode([(8, i % 2) for i in range(5000)]).is_valid)
        self.assertFalse(self.decode([(256, 0)] * 1000).is_valid)
        self.assertFalse(self.decode(self.pulses(frame, half=64)).is_valid)
        self.assertFalse(self.decode(self.pulses(frame, half=128)).is_valid)

    def test_resync_after_gap_and_noisy_start(self):
        uid = 0x5e0028cd0a
        frame = self.lib.lf_rfid_decoder_encode(uid)
        prefix = self.pulses(frame, repeats=1) + [(10000, 0), (7, 1), (900, 0)]
        result = self.decode(prefix + self.pulses(frame))
        self.assertTrue(result.is_valid)
        self.assertEqual(result.id, uid)

if __name__ == "__main__": unittest.main()
