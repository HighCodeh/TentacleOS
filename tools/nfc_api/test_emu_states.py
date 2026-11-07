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

"""Run the actual emulator state machine against scripted reader events."""
from pathlib import Path
import re
import subprocess
import tempfile
from test_emu_tx import extract, ROOT, MIFARE
source = (MIFARE / "mf_classic_emu.c").read_text()
header = (MIFARE / "include/mf_classic_emu.h").read_text()
constants = "\n".join(re.findall(r"^#define (?:PTA_|MFC_|MF_|FIFO_)[^\n]+", source + "\n" + header, re.M))
a = header.index("  MFC_EMU_STATE_IDLE")
b = header.index("}", a)
states = "typedef enum {\n" + header[a:b] + "} mfc_emu_state_t;"
functions = "\n".join(extract(source, name) for name in (
    "static hb_nfc_err_t target_enter_listen(void) {",
    "static hb_nfc_err_t target_enter_sleep(void) {",
    "mfc_emu_state_t mfc_emu_run_step(void) {",
))
fixture = Path(__file__).with_suffix(".c").read_text().replace("/* CONSTANTS */", constants + "\n" + states).replace("/* PRODUCTION */", functions)
with tempfile.TemporaryDirectory(prefix="nfc-state-") as tmp:
    p = Path(tmp)
    (p / "test.c").write_text(fixture)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    "-I", str(ROOT / "firmware_p4/components/Drivers/st25r3916/include"),
                    str(p / "test.c"), "-o", str(p / "test")], check=True)
    subprocess.run([str(p / "test")], check=True)
