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

"""Reject corrupt anticollision replies and verify reader-to-target reset failures."""
from pathlib import Path
import re
import subprocess
import tempfile
from source import function

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "firmware_p4/components/Applications/nfc"

def main():
    poller = (BASE / "protocols/iso14443a/poller.c").read_text()
    listener = (BASE / "nfc_listener.c").read_text()
    constants = "\n".join(re.findall(r"^#define (?:ANTICOLL_[^\n]+)", poller, re.M))
    production = "\n".join((
        function(poller, "int iso14443a_poller_anticoll("),
        function(listener, "static hb_nfc_err_t configure_hardware(void) {"),
    ))
    fixture = Path(__file__).with_suffix(".c").read_text()
    fixture = fixture.replace("/* CONSTANTS */", constants).replace("/* PRODUCTION */", production)
    with tempfile.TemporaryDirectory(prefix="tos-nfc-activation-") as tmp:
        directory = Path(tmp)
        (directory / "test.c").write_text(fixture)
        subprocess.run(["cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
            "-fsanitize=undefined,bounds", str(directory / "test.c"),
            "-o", str(directory / "test")], check=True, timeout=60)
        subprocess.run([str(directory / "test")], check=True, timeout=30)

if __name__ == "__main__":
    main()
