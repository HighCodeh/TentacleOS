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

"""Check scoped Classic nonce CRC exception in the production poller."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "firmware_p4/components/Applications/nfc/protocols/iso14443a"


def main():
    source = (BASE / "nfc_poller.c").read_text()
    code = source[source.index("typedef struct {"):source.index("void nfc_poller_set_timing(")]
    fixture = Path(__file__).with_suffix(".c").read_text().replace("/* PRODUCTION */", code)
    with tempfile.TemporaryDirectory(prefix="nfc-isodep-") as tmp:
        p = Path(tmp)
        (p / "test.c").write_text(fixture)
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall",
                        "-Wextra", "-Werror", "-Wno-unused-variable",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                        str(p / "test.c"), "-o", str(p / "test")], check=True)
        subprocess.run([str(p / "test")], check=True)


if __name__ == "__main__":
    main()
