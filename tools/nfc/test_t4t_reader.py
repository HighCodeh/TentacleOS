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

"""Exercise production Type 4 file operations against an APDU memory fixture."""
import re, subprocess, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
base=root/'firmware_p4/components/Applications/nfc/protocols/iso14443a'
source=re.sub(r"^#include[^\n]*", "", (base/'t4t.c').read_text(), flags=re.M)
header=(base/'include/t4t.h').read_text()
type_cc=re.search(r"typedef struct \{.*?\} t4t_cc_t;",header,re.S).group()
preamble=r"""
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
typedef int hb_nfc_err_t;
typedef struct{int unused;}nfc_iso_dep_data_t;
#define HB_NFC_OK 0
#define HB_NFC_ERR_PARAM 1
#define HB_NFC_ERR_PROTOCOL 2
static void ignore(const char*t,const char*f,...){(void)t;(void)f;}
#define ESP_LOGI ignore
#define ESP_LOGW ignore
static int iso_dep_apdu_transceive(const nfc_iso_dep_data_t*,const uint8_t*,size_t,uint8_t*,size_t,int);
"""
with tempfile.TemporaryDirectory() as d:
    exe=str(Path(d)/'t4t')
    program=preamble+type_cc+source+Path(__file__).with_suffix('.c').read_text()
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined,bounds','-fno-sanitize-recover=all','-x','c','-','-o',exe],input=program,text=True,check=True)
    subprocess.run([exe],check=True)
