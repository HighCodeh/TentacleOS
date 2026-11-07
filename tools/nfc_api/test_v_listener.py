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

"""Exercise the production NFC-V/SLIX listener with ASan/UBSan."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'firmware_p4/components/Applications/nfc/protocols/iso15693'
with tempfile.TemporaryDirectory(prefix='nfc-v-listener-') as tmp:
    p = Path(tmp)
    (p/'esp_err.h').write_text('typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n')
    (p/'esp_log.h').write_text('#define ESP_LOGE(tag,...) ((void)(tag))\n')
    (p/'esp_random.h').write_text('#include <stddef.h>\nvoid esp_fill_random(void *, size_t);\n')
    subprocess.run([*shlex.split(os.environ.get('CC','cc')), '-std=c11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I'+str(p), '-I'+str(BASE/'include'), str(BASE/'iso15693_listener.c'),
                    str(Path(__file__).with_suffix('.c')), '-o', str(p/'test')], check=True)
    subprocess.run([str(p/'test')], check=True)
