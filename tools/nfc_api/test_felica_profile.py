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

"""FeliCa model, reader, listener, crypto and fork file regression tests."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'firmware_p4/components/Applications/nfc'

def main():
    with tempfile.TemporaryDirectory(prefix='nfc-felica-') as temp:
        p = Path(temp)
        (p/'esp_err.h').write_text("""#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
#define ESP_ERR_NOT_FOUND -3
#define ESP_ERR_INVALID_RESPONSE -4
#define ESP_ERR_NO_MEM -5
#define ESP_ERR_INVALID_SIZE -6
#define ESP_ERR_NOT_SUPPORTED -7
#define ESP_ERR_INVALID_STATE -8
#define ESP_ERR_TIMEOUT -9
#define ESP_ERR_NOT_ALLOWED -10
""")
        (p/'esp_log.h').write_text('#pragma once\n#define ESP_LOGE(tag,...) ((void)(tag))\n#define ESP_LOGW(tag,...) ((void)(tag))\n#define ESP_LOGD(tag,...) ((void)(tag))\n')
        (p/'esp_random.h').write_text('#include <stddef.h>\nvoid esp_fill_random(void *,size_t);\n')
        (p/'nfc_poller.h').write_text('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\nint nfc_poller_transceive(const uint8_t *,size_t,bool,uint8_t *,size_t,int,int);\n')
        (p/'storage_atomic.h').write_text('#include <stddef.h>\nint storage_write_atomic(const char *,const void *,size_t);\n')
        for name in ('highboy_nfc_types.h','highboy_nfc_compat.h'):
            (p/name).write_text('#pragma once\n')
        (p/'highboy_nfc_error.h').write_text('#include "esp_err.h"\ntypedef esp_err_t hb_nfc_err_t;\n')
        notice = (BASE/'nfc_felica.c').read_text().split('#include')[0]
        for header in p.glob('*.h'):
            header.write_text(notice + header.read_text())
        sources = ['nfc_felica.c','nfc_felica_file.c','nfc_felica_crypto.c','nfc_felica_listener.c',
                   'protocols/common/legacy_des/des.c']
        command = [*shlex.split(os.environ.get('CC','cc')), '-std=c11', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-I'+str(p),'-I'+str(BASE),'-I'+str(BASE/'include'),
                   '-I'+str(BASE/'protocols/felica/include')]
        subprocess.run(command + [str(BASE/s) for s in sources] +
                       [str(Path(__file__).with_suffix('.c')),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test'),str(p/'card.nfc')],check=True)

if __name__ == '__main__':
    main()
