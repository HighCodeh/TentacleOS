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

"""Check production reader unlock state transitions without an RF card."""
from pathlib import Path
import os,shlex,subprocess,tempfile
from test_emu_tx import extract
ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/'firmware_p4/components/Applications/nfc'
s=(BASE/'nfc_protocol_service.c').read_text()
code=extract(s,'static bool ul_type(')+extract(s,'static esp_err_t read_ul_profile(')
fixture=r"""
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <assert.h>
#include "nfc_t2t_file.h"
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_ERR_NOT_SUPPORTED 4
#define ESP_ERR_INVALID_RESPONSE 5
#define HB_NFC_OK 0
#define HB_NFC_ERR_PROTOCOL 6
#define UL_VERSION_SIZE 8
#define UL_CRC_BYTES 2
#define UL_PASSWORD_BYTES 4
#define UL_PACK_BYTES 2
#define UL_COUNTER_BYTES 3
#define UL_CMD_SIGNATURE 0x3C
#define UL_CMD_COUNTER 0x39
#define UL_CMD_TEARING 0x3E
#define UL_AES_PAGE_SIZE 4
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define ESP_LOGE(...) ((void)0)
#define heap_caps_calloc(n,s,c) calloc(n,s)
typedef struct {uint8_t uid[10],uid_len;} nfc_iso14443a_data_t;
typedef struct {unsigned line_count;} nfc_protocol_result_t;
static nfc_t2t_file_t s_ul_card;
static bool s_has_ul_card,s_ul_candidate_pending;
static uint8_t s_ul_candidate[4];
static char s_aes_last_path[]="/sdcard/nfc/dumps/test.nfc";
static unsigned auth_calls,read_calls,save_calls;
static int auth_result=0,read_pages=16;
static uint8_t uid[7]={4,42,245,234,32,31,145};
static void add_line(nfc_protocol_result_t *r,const char *fmt,...) {(void)fmt;r->line_count++;}
static bool cancel_requested(void) {return false;}
static void format_uid(const uint8_t *u,uint8_t l,bool r,char *s,size_t n) {(void)u;(void)l;(void)r;snprintf(s,n,"test");}
static uint16_t mful_version_pages(const uint8_t *v) {return v[6]==0x0B?20:0;}
static int mful_pwd_auth(const uint8_t *p,uint8_t *pack) {auth_calls++;assert(p[0]==1);pack[0]=0xAB;pack[1]=0xCD;return auth_result;}
static int mful_read_all(uint8_t *out,uint16_t total) {(void)total;read_calls++;memcpy(out,uid,3);memcpy(out+4,uid+3,4);return read_pages;}
static int iso14443a_poller_reselect(nfc_iso14443a_data_t *c) {(void)c;return 0;}
static bool ul_metadata(uint8_t c,uint8_t i,uint8_t *out,size_t n) {(void)c;(void)i;(void)out;(void)n;return false;}
static int save_aes_card(const nfc_t2t_file_t *c) {(void)c;save_calls++;return 0;}
/* PRODUCTION */
int main(void) {
 nfc_iso14443a_data_t card={.uid_len=7};memcpy(card.uid,uid,7);
 uint8_t version[8]={0,4,3,1,1,0,0x0B,3};nfc_protocol_result_t result={0};
 assert(read_ul_profile(&card,version,&result)==0);
 assert(s_has_ul_card && s_ul_card.config.readable_pages==16 && save_calls==1 && auth_calls==0);
 s_ul_candidate_pending=true;s_ul_candidate[0]=1;
 card.uid_len=10;
 assert(read_ul_profile(&card,version,&result)==HB_NFC_ERR_PROTOCOL);
 assert(auth_calls==0 && s_ul_candidate_pending && read_calls==1 && save_calls==1);
 card.uid_len=7;
 card.uid[0]^=1;
 assert(read_ul_profile(&card,version,&result)==ESP_ERR_INVALID_STATE);
 assert(auth_calls==0 && s_ul_candidate_pending);card.uid[0]^=1;
 version[4]^=1;
 assert(read_ul_profile(&card,version,&result)==ESP_ERR_INVALID_STATE && auth_calls==0);
 version[4]^=1;
 assert(read_ul_profile(&card,version,&result)==ESP_ERR_INVALID_RESPONSE);
 assert(auth_calls==1 && !s_ul_candidate_pending && s_ul_candidate[0]==0);
 assert(read_ul_profile(&card,version,&result)==0 && auth_calls==1);
 s_ul_candidate_pending=true;s_ul_candidate[0]=1;auth_result=2;read_pages=20;
 assert(read_ul_profile(&card,version,&result)==0 && auth_calls==2);
 assert(!s_ul_card.config.readable_pages && s_ul_card.config.enable_pwd);
 assert(s_ul_card.memory[72]==1 && s_ul_card.memory[76]==0xAB && s_ul_card.memory[77]==0xCD);
 assert(!s_ul_candidate_pending);
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix='ul-unlock-') as tmp:
 p=Path(tmp)
 (p/'highboy_nfc_error.h').write_text('typedef int hb_nfc_err_t;\n')
 (p/'highboy_nfc_compat.h').write_text('typedef int esp_err_t;\n')
 (p/'esp_err.h').write_text('typedef int esp_err_t;\n')
 (p/'test.c').write_text(fixture.replace('/* PRODUCTION */',code))
 subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(p),'-I'+str(BASE/'include'),'-I'+str(BASE/'protocols/t2t/include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,timeout=20)
print('PASS: partial capture, UID/version guard, one-shot password attempt, rejection clears candidate, verified memory and PACK')
