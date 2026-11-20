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

"""Production Classic reader must reject invalid At, parity and bus failures."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
from test_emu_tx import extract
from test_readers import defines
ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "firmware_p4/components/Applications/nfc/protocols/mifare"


def main():
    source = (BASE / "mf_classic.c").read_text()
    funcs = "\n".join(extract(source, name) for name in (
        "static void bit_set(", "static uint8_t bit_get(", "static uint32_t bytes_to_num_be(",
        "static uint64_t key_to_u64_be(", "static uint32_t mf_rand32(",
        "static bool auth_response_valid(", "hb_nfc_err_t mf_classic_auth("))
    fixture = Path(__file__).with_suffix(".c").read_text()
    fixture = fixture.replace("/* CONSTANTS */", defines(source)).replace("/* PRODUCTION */", funcs)
    with tempfile.TemporaryDirectory(prefix="nfc-classic-auth-") as tmp:
        p = Path(tmp)
        (p / "test.c").write_text(fixture)
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall",
                        "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        "-fno-sanitize-recover=all", "-I", str(BASE / "include"),
                        str(p / "test.c"), str(BASE / "crypto1.c"),
                        "-o", str(p / "test")], check=True)
        subprocess.run([str(p / "test")], check=True)


if __name__ == "__main__":
    main()
