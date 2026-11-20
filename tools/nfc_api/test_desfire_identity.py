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

"""Test production identity configuration and RF cleanup without hardware."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
from test_emu_tx import extract
ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT/'firmware_p4/components/Applications/nfc'

def main():
    source = (BASE/'protocols/iso14443a/t4t_emu.c').read_text()
    service = (BASE/'nfc_protocol_service.c').read_text()
    fixture = Path(__file__).with_suffix('.c').read_text()
    fixture = fixture.replace('/* IDENTITY */', extract(source, 'hb_nfc_err_t t4t_emu_init_identity('))
    fixture = fixture.replace('/* LIFECYCLE */', extract(service, 'static esp_err_t emulate_desfire('))
    with tempfile.TemporaryDirectory(prefix='desfire-identity-') as temp:
        p = Path(temp)
        (p/'test.c').write_text(fixture)
        subprocess.run([*shlex.split(os.environ.get('CC','cc')), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined','-fno-sanitize-recover=all', str(p/'test.c'), '-o', str(p/'test')], check=True)
        subprocess.run([str(p/'test')], check=True)

if __name__ == '__main__':
    main()
