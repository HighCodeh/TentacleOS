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

"""Compile and run the real Sub-GHz codecs with RAM-only hardware/storage fakes."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOURCE = ROOT / "firmware_p4/components/Applications/SubGhz"
STUBS = HERE / "test_subghz_stubs"


def main():
    sources = sorted((SOURCE / "protocols").glob("*.c"))
    sources += [SOURCE / name for name in (
        "subghz_keeloq.c", "subghz_protocol_serializer.c", "subghz_replay.c")]
    sources.append(ROOT / "firmware_p4/components/Drivers/cc1101/cc1101_config.c")
    with tempfile.TemporaryDirectory(prefix="highboy-subghz-") as temp:
        binary = Path(temp) / "test_subghz"
        command = [os.environ.get("CC", "cc"), "-std=c11", "-D_POSIX_C_SOURCE=200809L",
                   "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                   "-fno-sanitize-recover=all", "-g", "-I", str(STUBS),
                   "-I", str(SOURCE / "include"),
                   "-I", str(ROOT / "firmware_p4/components/Drivers/cc1101/include"),
                   "-I", str(SOURCE / "protocols/include"),
                   str(HERE / "test_subghz.c"), *map(str, sources), "-o", str(binary)]
        subprocess.run(command, check=True)
        subprocess.run([str(binary)], check=True, timeout=30)


if __name__ == "__main__":
    main()
