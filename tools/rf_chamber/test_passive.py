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

"""Fault injection for the production passive boot, storage and GPIO service."""
from pathlib import Path
import re, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[2]

def main():
    service=ROOT/'firmware_p4/components/Service/rf_chamber/rf_passive.c'
    native=re.sub(r'^#include .*$', '', service.read_text(), flags=re.M)
    fixture=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */', native)
    with tempfile.TemporaryDirectory(prefix='rf-passive-') as tmp:
        path=Path(tmp);(path/'test.c').write_text(fixture)
        subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',str(path/'test.c'),'-o',str(path/'test')],check=True)
        subprocess.run([str(path/'test')],check=True)
if __name__=='__main__': main()
