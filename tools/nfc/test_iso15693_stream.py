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

"""Exercise NFC-V wire framing, CRC and rejection of malformed/truncated responses."""
from pathlib import Path
import re
import subprocess
import tempfile
from source import function

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "firmware_p4/components/Applications/nfc/protocols/iso15693"

def main():
    source = (BASE / "iso15693.c").read_text()
    constants = "\n".join(re.findall(r"^#define ISO15693_[^\n]+", source, re.M))
    functions = "\n".join(function(source, signature) for signature in (
        "uint16_t iso15693_crc16(", "bool iso15693_check_crc(",
        "int iso15693_transceive(const uint8_t *tx, size_t tx_len, bool with_crc,\n                            uint8_t *rx, size_t rx_max, size_t rx_min, int timeout_ms) {"))
    fixture = Path(__file__).with_suffix(".c").read_text()
    fixture = fixture.replace("/* CONSTANTS */", constants).replace("/* PRODUCTION */", functions)
    with tempfile.TemporaryDirectory(prefix="tos-nfc-v-") as tmp:
        directory = Path(tmp)
        (directory / "test.c").write_text(fixture)
        subprocess.run(["cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=undefined,bounds", "-I", str(BASE / "include"),
            str(directory / "test.c"), str(BASE / "iso15693_stream.c"),
            "-o", str(directory / "test")], check=True, timeout=60)
        subprocess.run([str(directory / "test")], check=True, timeout=30)

if __name__ == "__main__":
    main()
