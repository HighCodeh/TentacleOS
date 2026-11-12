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

"""Exercise production Classic value/transfer handlers, including silent RX rearm."""
from pathlib import Path
import re, subprocess, tempfile
from source import function
ROOT=Path(__file__).resolve().parents[2]
src=(ROOT/'firmware_p4/components/Applications/nfc/protocols/mifare/mf_classic_emu.c').read_text()
constants="\n".join(re.findall(r"^#define (?:MFC_|MF_)[^\n]+",src,re.M))
header=(ROOT/'firmware_p4/components/Applications/nfc/protocols/mifare/include/mf_classic_emu.h').read_text()
constants+="\n"+"\n".join(re.findall(r"^#define MFC_(?:CMD_|ACK|NACK)[^\n]+",header,re.M))
functions=["static void reset_crypto_state(void) {", "static bool is_value_block_format(const uint8_t *data) {", "static int32_t read_value_from_block(const uint8_t *data) {", "static void write_value_to_block(uint8_t *data, int32_t value, uint8_t addr) {", "static mfc_emu_state_t handle_value_op_phase2(const uint8_t *data, int len) {", "static mfc_emu_state_t handle_transfer(uint8_t block_num) {"]
production="\n".join(function(src,n) for n in functions)
fixture=Path(__file__).with_suffix('.c').read_text().replace('/* CONSTANTS */',constants).replace('/* PRODUCTION */',production)
with tempfile.TemporaryDirectory(prefix='tos-value-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(fixture)
 subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=undefined,bounds',str(p/'test.c'),'-o',str(p/'test')],check=True,timeout=60)
 subprocess.run([str(p/'test')],check=True,timeout=30)
