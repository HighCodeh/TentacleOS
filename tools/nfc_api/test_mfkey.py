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

"""Verify production recovery against independently generated known-answer vectors."""
from pathlib import Path
import os, shlex, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/"firmware_p4/components/Applications/nfc/protocols/mifare"
with tempfile.TemporaryDirectory(prefix="nfc-mfkey-") as tmp:
    tmp=Path(tmp)
    cc=[*shlex.split(os.environ.get("CC","cc")), "-O2", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I"+str(BASE/"include")]
    subprocess.run([*cc,"-Dmalloc=mfkey_test_malloc","-Dfree=mfkey_test_free","-c",str(BASE/"mfkey.c"),"-o",str(tmp/"mfkey.o")],check=True)
    subprocess.run([*cc,str(tmp/"mfkey.o"),str(BASE/"crypto1.c"),str(Path(__file__).with_suffix(".c")),"-o",str(tmp/"test")],check=True)
    subprocess.run([str(tmp/"test")],check=True,timeout=120)
