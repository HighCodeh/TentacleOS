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

"""Check real encrypted target TX bit counts with the real Crypto1 implementation."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
MIFARE = ROOT / "firmware_p4/components/Applications/nfc/protocols/mifare"


def extract(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    source = (MIFARE / "mf_classic_emu.c").read_text()
    names = (
        "static hb_nfc_err_t target_tx_raw(",
        "static void crypto1_encrypt_with_parity(",
        "static hb_nfc_err_t target_tx_encrypted(",
        "static hb_nfc_err_t target_tx_ack_encrypted(",
    )
    functions = "\n".join(extract(source, name) for name in names)
    fixture = Path(__file__).with_suffix(".c").read_text()
    fixture = fixture.replace("/* PRODUCTION */", functions)
    with tempfile.TemporaryDirectory(prefix="nfc-emu-tx-") as tmp:
        directory = Path(tmp)
        (directory / "test.c").write_text(fixture)
        subprocess.run([
            *shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
            "-Werror", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
            "-I", str(MIFARE / "include"), str(directory / "test.c"),
            str(MIFARE / "crypto1.c"), "-o", str(directory / "test"),
        ], check=True)
        subprocess.run([str(directory / "test")], check=True)


if __name__ == "__main__":
    main()
