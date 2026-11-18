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

"""Check production dictionary append including delayed flush/close failures."""
from pathlib import Path
import os,shlex,subprocess,tempfile
from test_emu_tx import extract
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/"firmware_p4/components/Applications/nfc/protocols/mifare/nfc_dict_loader.c").read_text()
fixture=Path(__file__).with_suffix(".c").read_text().replace("/* PRODUCTION */",extract(source,"esp_err_t nfc_dict_append_key("))
with tempfile.TemporaryDirectory(prefix="nfc-key-append-") as tmp:
 tmp=Path(tmp);(tmp/"t.c").write_text(fixture)
 subprocess.run([*shlex.split(os.environ.get("CC","cc")),"-std=c11","-Wall","-Wextra","-Werror","-fsanitize=undefined","-fno-sanitize-recover=all",str(tmp/"t.c"),"-o",str(tmp/"test")],check=True)
 subprocess.run([str(tmp/"test")],check=True)
