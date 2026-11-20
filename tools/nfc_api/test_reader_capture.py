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

"""Check the production emulator capture path without RF hardware."""
from pathlib import Path
import os,shlex,subprocess,tempfile
from test_emu_tx import extract
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/"firmware_p4/components/Applications/nfc/protocols/mifare/mf_classic_emu.c").read_text()
functions="\n".join(extract(source,name) for name in (
 "static uint32_t bytes_to_u32_be(","static void u32_to_bytes_be(","static uint32_t get_cuid(",
 "static mfc_emu_state_t capture_auth(",
 "static mfc_emu_state_t authenticate(uint8_t auth_cmd, uint8_t block_num) {",
 "static mfc_emu_state_t handle_auth(uint8_t auth_cmd, uint8_t block_num) {"))
fixture=Path(__file__).with_suffix(".c").read_text().replace("/* PRODUCTION */",functions)
with tempfile.TemporaryDirectory(prefix="nfc-reader-capture-") as tmp:
 tmp=Path(tmp);(tmp/"test.c").write_text(fixture)
 subprocess.run([*shlex.split(os.environ.get("CC","cc")),"-std=c11","-Wall","-Wextra","-Werror","-Wno-sign-compare","-fsanitize=undefined","-fno-sanitize-recover=all",str(tmp/"test.c"),"-o",str(tmp/"test")],check=True)
 subprocess.run([str(tmp/"test")],check=True)
