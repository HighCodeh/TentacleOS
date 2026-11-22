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

"""Exercise production profile charge control with register-level I2C mocks."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
from test_shutdown import DRIVER, MOCKS, NOTICE

with tempfile.TemporaryDirectory(prefix="highboy-charge-profile-") as directory:
    work = Path(directory)
    for name, body in MOCKS.items():
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(NOTICE + body)
    command = [*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra",
               "-Werror", "-Wno-unused-function", "-fsanitize=address,undefined",
               "-fno-sanitize-recover=all", "-DCONFIG_HIGHBOY_BOARD_V203=1",
               "-I" + str(work), "-I" + str(DRIVER), "-I" + str(DRIVER / "include"),
               str(Path(__file__).with_suffix(".c")), "-o", str(work / "test")]
    subprocess.run(command, check=True)
    subprocess.run([str(work / "test")], check=True, timeout=30)
