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

"""Run production USB role arbitration with SDK fakes and sanitizers."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / "firmware_p4/components/Drivers/tusb_desc/tusb_desc.c").read_text()
role = source[source.index("#define USB_ROLE_WAIT_MS"):source.index("static esp_err_t init_locked")]
fixture = (Path(__file__).with_name("usb_role_test.c")).read_text()
with tempfile.TemporaryDirectory(prefix="highboy-usb-role-") as directory:
    path = Path(directory)
    (path / "test.c").write_text(fixture.replace("/* ROLE_IMPLEMENTATION */", role))
    subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True, timeout=30)
