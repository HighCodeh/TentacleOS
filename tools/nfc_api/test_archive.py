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

"""Exercise complete archive round trips and malformed document rejection."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'firmware_p4/components/Applications/nfc'
with tempfile.TemporaryDirectory(prefix='nfc-archive-') as tmp:
    p = Path(tmp)
    notice = (BASE/'nfc_archive.c').read_text().split('#include')[0]
    errors = ['ESP_OK','ESP_ERR_INVALID_ARG','ESP_ERR_NOT_FOUND','ESP_ERR_NO_MEM',
              'ESP_ERR_INVALID_RESPONSE','ESP_ERR_INVALID_SIZE','ESP_FAIL']
    (p/'esp_err.h').write_text(notice + 'typedef int esp_err_t;\n' +
        ''.join(f'#define {name} {value}\n' for value,name in enumerate(errors)))
    (p/'esp_log.h').write_text(notice + '#define ESP_LOGE(tag,...) ((void)(tag))\n')
    (p/'storage_atomic.h').write_text(notice + '#include <stddef.h>\nint storage_write_atomic(const char *,const void *,size_t);\n')
    header = (BASE/'include/nfc_archive.h').read_text().replace('/sdcard/nfc/dumps', str(p/'dumps'))
    (p/'nfc_archive.h').write_text(header)
    source = (BASE/'nfc_archive.c').read_text().replace('mkdir("/sdcard/nfc",', 'mkdir("'+str(p)+'",')
    (p/'nfc_archive.c').write_text(source)
    subprocess.run([*shlex.split(os.environ.get('CC','cc')), '-D_POSIX_C_SOURCE=200809L',
                    '-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all','-I'+str(p),str(p/'nfc_archive.c'),
                    str(Path(__file__).with_suffix('.c')), '-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test'),str(p)],check=True)
