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

"""Exercise production NFC reader parsers against malformed card responses."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
from test_emu_tx import extract

ROOT = Path(__file__).resolve().parents[2]
PROTOCOLS = ROOT / "firmware_p4/components/Applications/nfc/protocols"


def defines(source):
    return "\n".join(re.findall(r"^#define .*(?:\\\n.*)*", source, re.M))


def main():
    source = (PROTOCOLS / "felica/felica.c").read_text()
    header = (PROTOCOLS / "felica/include/felica.h").read_text()
    functions = "\n".join(extract(source, name) for name in (
        "static int frame_length(", "hb_nfc_err_t felica_sensf_req_slots(",
        "hb_nfc_err_t felica_read_blocks(", "hb_nfc_err_t felica_write_blocks("))
    fixture = Path(__file__).with_suffix(".c").read_text()
    fixture = fixture.replace("/* CONSTANTS */", defines(header) + "\n" + defines(source))
    fixture = fixture.replace("/* PRODUCTION */", functions)
    with tempfile.TemporaryDirectory(prefix="nfc-readers-") as tmp:
        directory = Path(tmp)
        test = directory / "test.c"
        test.write_text(fixture)
        subprocess.run([
            *shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
            "-Werror", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            str(test), "-o", str(directory / "test"),
        ], check=True)
        subprocess.run([str(directory / "test")], check=True)


if __name__ == "__main__":
    main()
