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

"""Exercise the Ultralight AES workflow with a deterministic card transport."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
NFC = ROOT / 'firmware_p4/components/Applications/nfc'

def main():
    from test_emu_tx import extract
    service = (NFC/'nfc_protocol_service.c').read_text()
    source = '\n'.join(extract(service, signature) for signature in
                       ('static esp_err_t capture_aes(', 'static hb_nfc_err_t write_ul_aes('))
    fixture = Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */', source)
    with tempfile.TemporaryDirectory(prefix='plus-poller-') as tmp:
        path = Path(tmp)
        (path/'test.c').write_text(fixture)
        subprocess.run([*shlex.split(os.environ.get('CC','cc')), '-std=c11', '-Wall', '-Wextra',
                        '-Werror', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        str(path/'test.c'), '-o', str(path/'test')], check=True)
        subprocess.run([str(path/'test')], check=True)

if __name__ == '__main__':
    main()
