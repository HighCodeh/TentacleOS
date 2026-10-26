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

"""Run the retained upstream NFC DES/3DES vectors with host sanitizers."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "firmware_p4/components/Applications/nfc/protocols/common/legacy_des"


def main():
    with tempfile.TemporaryDirectory(prefix="nfc-des-") as temporary:
        work = Path(temporary)
        notice = Path(__file__).read_text().split("\n\n", 1)[0].split("\n", 1)[1]
        c_notice = "\n".join("//" + line[1:] for line in notice.splitlines())
        (work / "main.c").write_text(c_notice + '\n#include "des.h"\n'
                                    'int main(void) { return mbedtls_des_self_test(1); }\n')
        executable = work / "test"
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11",
                        "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                        "-fno-sanitize-recover=all", "-DTOS_DES_SELF_TEST", "-I", str(SOURCE),
                        str(work / "main.c"), str(SOURCE / "des.c"), "-o", str(executable)],
                       check=True, timeout=60)
        subprocess.run([str(executable)], check=True, timeout=30)


if __name__ == "__main__":
    main()
