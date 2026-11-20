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

"""Exercise production capture/recovery orchestration with scripted dependencies."""
from pathlib import Path
import os,re,shlex,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2];BASE=ROOT/"firmware_p4/components/Applications/nfc"
def strip(text):return re.sub(r'^#include.*$','',text,flags=re.M)
headers=strip((BASE/"protocols/mifare/include/mfkey.h").read_text())+strip((BASE/"include/nfc_mfkey.h").read_text())
fixture=Path(__file__).with_suffix(".c").read_text().replace("/* HEADERS */",headers).replace("/* PRODUCTION */",strip((BASE/"nfc_mfkey.c").read_text()))
with tempfile.TemporaryDirectory(prefix="nfc-mfkey-workflow-") as tmp:
 tmp=Path(tmp);(tmp/"test.c").write_text(fixture)
 subprocess.run([*shlex.split(os.environ.get("CC","cc")),"-std=c11","-D_POSIX_C_SOURCE=200809L","-Wall","-Wextra","-Werror","-Wno-unused-variable","-fsanitize=undefined","-fno-sanitize-recover=all",str(tmp/"test.c"),"-o",str(tmp/"test")],check=True)
 subprocess.run([str(tmp/"test")],cwd=tmp,check=True)
