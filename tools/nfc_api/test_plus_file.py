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

"""Validate the real Plus file importer with sanitizers and malformed files."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/'firmware_p4/components/Applications/nfc'
def main():
    with tempfile.TemporaryDirectory(prefix='nfc-plus-file-') as tmp:
        p=Path(tmp)
        (p/'t4t_emu.h').write_text("""#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define MFP_BLOCK_SIZE 16U
#define MFP_VERSION_SIZE 28U
#define MFP_SIGNATURE_SIZE 56U
#define T4T_EMU_ATS_CAPACITY 32U
typedef struct {uint8_t uid[7],uid_len,atqa[2],sak,ats[32],ats_len;} t4t_emu_identity_t;
typedef struct {
 void *context;
 bool (*key)(void *,uint16_t,uint8_t *);
 bool (*read)(void *,uint16_t,uint16_t,uint8_t *);
 bool (*write)(void *,uint16_t,uint16_t,const uint8_t *);
 const uint8_t *version,*signature;
} mfp_listener_config_t;
""")
        (p/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n#define ESP_ERR_INVALID_ARG -2\n#define ESP_ERR_NOT_FOUND -3\n#define ESP_ERR_INVALID_RESPONSE -4\n#define ESP_ERR_NO_MEM -5\n#define ESP_ERR_INVALID_SIZE -6\n')
        (p/'esp_log.h').write_text('#pragma once\n#define ESP_LOGD(tag,...) ((void)(tag))\n#define ESP_LOGE(tag,...) ((void)(tag))\n')
        (p/'storage_atomic.h').write_text('#include <stddef.h>\nint storage_write_atomic(const char *,const void *,size_t);\n')
        subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-Wall','-Wextra','-Werror',
                        '-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(p),
                        '-I'+str(BASE/'include'),str(BASE/'nfc_plus_file.c'),
                        str(Path(__file__).with_suffix('.c')),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test'),str(p/'card.nfc')],check=True)
if __name__=='__main__':
    main()
