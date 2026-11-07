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

"""Compile and exercise the strict NFC-V dump importer with sanitizers."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
from test_emu_tx import extract
ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'firmware_p4/components/Applications/nfc'
VALID = """Filetype: Flipper NFC device
Version: 4
Device type: SLIX
UID: E0 04 01 08 49 D0 DC 81
DSFID: 01
AFI: 3D
IC Reference: 01
Lock DSFID: false
Lock AFI: true
Block Count: 2
Block Size: 04
Data Content: 00 01 02 03 04 05 06 07
Security Status: 00 01
Capabilities: Default
Password Privacy: 01 02 03 04
Privacy Mode: false
Protection Pointer: 01
Protection Condition: 02
Lock EAS: false
Lock PPL: true
EAS Enabled: true
EAS ID: 12 34
EAS Protected: true
AFI Protected: false
Password 64: true
Password Locks: 00 01 00 00 00
"""
with tempfile.TemporaryDirectory(prefix='nfc-v-file-') as tmp:
    p = Path(tmp)
    (p/'esp_err.h').write_text('typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n#define ESP_ERR_NOT_FOUND 2\n#define ESP_ERR_NO_MEM 3\n#define ESP_ERR_INVALID_RESPONSE 4\n#define ESP_ERR_INVALID_STATE 5\n')
    (p/'storage_atomic.h').write_text('#include <stddef.h>\nint storage_write_atomic(const char *,const void *,size_t);\n')
    (p/'esp_log.h').write_text('#define ESP_LOGE(tag,...) ((void)(tag))\n')
    service = (BASE/'nfc_protocol_service.c').read_text()
    snapshot = extract(service, 'esp_err_t nfc_protocol_service_get_v_result(')
    fixture = Path(__file__).with_suffix('.c').read_text().replace('/* SNAPSHOT */', snapshot)
    (p/'test.c').write_text(fixture)
    subprocess.run([*shlex.split(os.environ.get('CC','cc')), '-std=c11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I'+str(p), '-I'+str(BASE/'include'),
                    '-I'+str(BASE/'protocols/iso15693/include'), str(BASE/'nfc_v_file.c'),
                    str(p/'test.c'), '-o', str(p/'test')], check=True)
    cases = [VALID, VALID + 'UID: E0 04 01 08 49 D0 DC 81\n',
             VALID.replace('00 01 02 03 04 05 06 07', '00 01'),
             VALID.replace('Security Status: 00 01', 'Security Status: 00 02'),
             VALID.replace('Block Count: 2', 'Block Count: 99999999999999999'),
             VALID.replace('Capabilities: Default', 'Capabilities: AcceptAllPasswords'),
             VALID.replace('Protection Condition: 02', 'Protection Condition: FF'),
             VALID.replace('Lock PPL: true\n', ''),
             VALID.replace('Password Privacy: 01 02 03 04', 'Password Privacy: GG 02 03 04'),
             VALID.replace('Block Size: 04', 'Block Size: 00'),
             VALID.replace('Data Content: ', 'Data Content: ' + '00 ' * 9000),
             VALID.replace('Version: 4', 'Version: 3'),
             VALID.replace('EAS ID: 12 34\n', ''),
             VALID.replace('EAS ID: 12 34', 'EAS ID: 12 34 56'),
             VALID.replace('Password Locks: 00 01 00 00 00', 'Password Locks: 00 02 00 00 00'),
             VALID.replace('Device type: SLIX', 'Device type: ISO15693-3')]
    paths = []
    for i, contents in enumerate(cases):
        path = p / f'{i}.nfc'
        path.write_text(contents)
        paths.append(str(path))
    subprocess.run([str(p/'test'), *paths], check=True)
