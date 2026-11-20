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

"""Check ST25TB one-shot writes, tag identity, confirmation staging and read-back."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
from test_emu_tx import extract
from test_readers import defines
ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "firmware_p4/components/Applications/nfc"


def main():
    source = (BASE / "nfc_protocol_service.c").read_text()
    crc = (BASE / "protocols/iso15693/iso15693.c").read_text()
    code = "\n".join([defines(source), defines(crc),
        extract(crc, "uint16_t iso15693_crc16("),
        extract(crc, "bool iso15693_check_crc("),
        extract(source, "static hb_nfc_err_t st25tb_exchange("),
        extract(source, "static hb_nfc_err_t write_st25tb("),
        extract(source, "hb_nfc_err_t nfc_protocol_service_st25tb_write(")])
    fixture = Path(__file__).with_suffix(".c").read_text().replace("/* PRODUCTION */", code)
    with tempfile.TemporaryDirectory(prefix="nfc-st25tb-") as tmp:
        p = Path(tmp)
        (p / "test.c").write_text(fixture)
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall",
                        "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        "-fno-sanitize-recover=all", str(p / "test.c"),
                        "-o", str(p / "test")], check=True)
        subprocess.run([str(p / "test")], check=True)


if __name__ == "__main__":
    main()
