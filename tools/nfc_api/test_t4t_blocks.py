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

"""Exercise production ISO-DEP target block handling with ASan/UBSan."""
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
    source = (BASE / "protocols/iso14443a/t4t_emu.c").read_text()
    code = "\n".join(extract(source, name) for name in [
        "static void reset_link(void) {", "static void send_fragment(void) {", "static void send_i_block_resp(const uint8_t *inf, int inf_len, uint8_t pcb_in) {", "static void send_r_ack(uint8_t pcb_in) {",
        "static void process_block("])
    fixture = Path(__file__).with_suffix(".c").read_text().replace("/* PRODUCTION */", code)
    constants = "\n".join(line for line in source.splitlines() if line.startswith("#define T4T_"))
    fixture = fixture.replace("/* DEFINES */", constants)
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
