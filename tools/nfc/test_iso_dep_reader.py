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

"""Compile the production ISO-DEP implementation with a scripted RF peer."""
import re, subprocess, tempfile, sys
from pathlib import Path
root = Path(__file__).resolve().parents[2]
source = Path(sys.argv[1]) if len(sys.argv) > 1 else root / "firmware_p4/components/Applications/nfc/protocols/iso14443a/iso_dep.c"
code = re.sub(r"^#include[^\n]*", "", source.read_text(), flags=re.M)
types = (root / "firmware_p4/components/Drivers/st25r3916/include/highboy_nfc_types.h").read_text()
match = re.search(r"typedef struct \{\s*uint8_t ats\[.*?\} highboy_nfc_iso_dep_t;", types, re.S)
assert match
preamble = r"""
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#define HIGHBOY_NFC_ATS_MAX_LEN 64
#define NFC_ATS_MAX_LEN 64
#define HB_NFC_OK 0
#define HB_NFC_ERR_PARAM 1
#define HB_NFC_ERR_PROTOCOL 2
typedef int hb_nfc_err_t;
static void ignore(const char*t,const char*f,...){(void)t;(void)f;}
#define ESP_LOGI ignore
#define ESP_LOGW ignore
static int64_t esp_timer_get_time(void);
static void hb_nfc_timer_delay_ms(uint32_t ms);
static bool iso14443a_check_crc(const uint8_t*d,size_t n);
static int nfc_poller_transceive(const uint8_t*t,size_t n,bool crc,uint8_t*r,size_t cap,size_t min,int ms);
"""
fixture = Path(__file__).with_suffix(".c").read_text()
with tempfile.TemporaryDirectory() as d:
    exe = str(Path(d) / "iso_dep")
    program = preamble + match.group() + "\ntypedef highboy_nfc_iso_dep_t nfc_iso_dep_data_t;\n" + code + fixture
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-fsanitize=undefined,bounds", "-fno-sanitize-recover=all", "-x", "c", "-", "-o", exe], input=program, text=True, check=True)
    subprocess.run([exe], check=True)
