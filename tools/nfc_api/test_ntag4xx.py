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

"""NTAG4xx version, model, typed file and optional capture tests."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'firmware_p4/components/Applications/nfc'

def main():
    with tempfile.TemporaryDirectory(prefix='nfc-ntag4xx-') as temp:
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
        (p/'storage_atomic.h').write_text('#include <stddef.h>\nint storage_write_atomic(const char *,const void *,size_t);\n')
        (p/'t4t_emu.h').write_text("""#pragma once
#include <stdint.h>
#define T4T_EMU_UID_CAPACITY 7U
#define T4T_EMU_ATS_CAPACITY 32U
typedef struct {uint8_t uid[7],uid_len,atqa[2],sak,ats[32],ats_len;} t4t_emu_identity_t;
""")
        (p/'mf_desfire.h').write_text("""#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#define MF_DESFIRE_CMD_GET_VERSION 0x60
#define MF_DESFIRE_SW_OK 0x9100
typedef struct {uint8_t uid[7],uid_len,atqa[2],sak;} nfc_iso14443a_data_t;
typedef struct {struct {uint8_t ats[32],ats_len;} dep;} mf_desfire_session_t;
int mf_desfire_transceive_native(mf_desfire_session_t *,uint8_t,const uint8_t *,size_t,uint8_t *,size_t,size_t *,uint16_t *);
""")
        notice = (BASE/'nfc_felica.c').read_text().split('#include')[0]
        for header in p.glob('*.h'):
            header.write_text(notice + header.read_text())
        sources = ['nfc_iso_file.c','nfc_ntag4xx.c','nfc_ntag4xx_file.c']
        command = [*shlex.split(os.environ.get('CC','cc')), '-std=c11', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-I'+str(p),'-I'+str(BASE),'-I'+str(BASE/'include'),
                   '-I'+str(BASE/'protocols/felica/include')]
        subprocess.run(command + [str(BASE/s) for s in sources] +
                       [str(Path(__file__).with_suffix('.c')),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test'),str(p/'card.nfc')],check=True)

if __name__ == '__main__':
    main()
