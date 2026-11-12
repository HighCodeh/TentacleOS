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

"""Type 4 NDEF listener and fork-compatible file round trips."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'firmware_p4/components/Applications/nfc'

def main():
    with tempfile.TemporaryDirectory(prefix='nfc-type4-') as temp:
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
        (p/'esp_log.h').write_text('#pragma once\n#define ESP_LOGI(tag,...) ((void)(tag))\n#define ESP_LOGE(tag,...) ((void)(tag))\n#define ESP_LOGW(tag,...) ((void)(tag))\n#define ESP_LOGD(tag,...) ((void)(tag))\n')
        (p/'storage_atomic.h').write_text('#include <stddef.h>\nint storage_write_atomic(const char *,const void *,size_t);\n')
        (p/'t4t_emu.h').write_text("""#pragma once
#include <stdint.h>
#define T4T_EMU_UID_CAPACITY 7U
#define T4T_EMU_ATS_CAPACITY 32U
typedef struct {uint8_t uid[7],uid_len,atqa[2],sak,ats[32],ats_len;} t4t_emu_identity_t;
""")
        (p/'t4t.h').write_text("""#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct {uint16_t cc_len,mle,mlc,ndef_fid; uint32_t ndef_max; uint8_t read_access,write_access,mapping,nlen_size; bool legacy;} t4t_cc_t;
typedef struct {uint8_t uid[7],uid_len,atqa[2],sak;} nfc_iso14443a_data_t;
""")
        (p/'mf_desfire.h').write_text("""#pragma once
#include "t4t.h"
#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>
#define MF_DESFIRE_SW_OK 0x9100
typedef struct {bool (*should_cancel)(void);} mf_desfire_session_t;
typedef struct {uint8_t hw[7],sw[7],uid[7],batch[7];} mf_desfire_version_t;
typedef struct {uint8_t aid[3];} mf_desfire_aid_t;
int mf_desfire_poller_init(const nfc_iso14443a_data_t *,mf_desfire_session_t *);
int mf_desfire_get_version(mf_desfire_session_t *,mf_desfire_version_t *);
int mf_desfire_select_application(mf_desfire_session_t *,const mf_desfire_aid_t *);
int mf_desfire_transceive_native(mf_desfire_session_t *,uint8_t,const uint8_t *,size_t,uint8_t *,size_t,size_t *,uint16_t *);
const char *esp_err_to_name(int);
""")
        (p/'iso_dep.h').write_text("""#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct {int unused;} nfc_iso_dep_data_t;
int iso_dep_apdu_transceive(const nfc_iso_dep_data_t *,const uint8_t *,size_t,uint8_t *,size_t,int);
""")
        (p/'nfc_common.h').write_text("""#pragma once
#include "esp_err.h"
#include <stdbool.h>
typedef int hb_nfc_err_t;
#define HB_NFC_OK ESP_OK
#define HB_NFC_ERR_PARAM ESP_ERR_INVALID_ARG
#define HB_NFC_ERR_PROTOCOL ESP_ERR_INVALID_RESPONSE
""")
        with (p/'t4t.h').open('a') as header:
            header.write("""
#include "iso_dep.h"
#include "nfc_common.h"
int t4t_read_cc(const nfc_iso_dep_data_t *,t4t_cc_t *);
int t4t_read_ndef(const nfc_iso_dep_data_t *,const t4t_cc_t *,uint8_t *,size_t,size_t *);
int t4t_write_ndef(const nfc_iso_dep_data_t *,const t4t_cc_t *,const uint8_t *,size_t);
int t4t_read_binary_ndef(const nfc_iso_dep_data_t *,const t4t_cc_t *,uint32_t,uint8_t *,size_t);
int t4t_update_binary_ndef(const nfc_iso_dep_data_t *,const t4t_cc_t *,uint32_t,const uint8_t *,size_t);
""")
        notice = (BASE/'nfc_felica.c').read_text().split('#include')[0]
        for header in p.glob('*.h'):
            header.write_text(notice + header.read_text())
        sources = ['nfc_iso_file.c','nfc_t4t.c','nfc_t4t_file.c','nfc_t4t_create.c']
        command = [*shlex.split(os.environ.get('CC','cc')), '-std=c11', '-Wall', '-Wextra', '-Werror',
                   '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                   '-I'+str(p),'-I'+str(BASE),'-I'+str(BASE/'include'),
                   '-I'+str(BASE/'protocols/felica/include')]
        subprocess.run(command + [str(BASE/s) for s in sources] +
                       [str(Path(__file__).with_suffix('.c')),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test'),str(p/'card.nfc')],check=True)
        subprocess.run(command + [str(BASE/'protocols/iso14443a/t4t.c'),
                       str(Path(__file__).with_name('test_t4t_io.c')),'-o',str(p/'io')],check=True)
        subprocess.run([str(p/'io')],check=True)

if __name__ == '__main__':
    main()
