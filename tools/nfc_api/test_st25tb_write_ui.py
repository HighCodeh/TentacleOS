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

"""Check ST25TB UI staging, confirmation, cancellation and refresh behavior."""
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
    source = (ROOT / "firmware_p4/components/Applications/ui/screens/nfc/nfc_protocol_lab_ui.c").read_text()
    code = "\n".join([
        extract(source, "static void timer_cb("),
        extract(source, "static void write_submit("),
        extract(source, "static void plus_key_submit("),
        extract(source, "static void plus_write_submit("),
        extract(source, "static void aes_write_submit("),
        extract(source, "static void slix_write_submit("),
        extract(source, "static void desfire_load_submit("),
        extract(source, "static void felica_load_submit("),
        extract(source, "static void ntag4xx_load_submit("),
        extract(source, "static void t4t_load_submit("),
        extract(source, "static void input_cb(")])
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
