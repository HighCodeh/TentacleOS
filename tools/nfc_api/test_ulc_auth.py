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

"""Check production Ultralight-C authentication against the NXP exchange vector."""
from pathlib import Path
import subprocess
import tempfile
from test_emu_tx import extract
ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / "firmware_p4/components/Applications/nfc/protocols"
source = (BASE / "t2t/t2t_emu.c").read_text()
names = ["static void rotate_left_8(uint8_t *out, const uint8_t *in) {",
         "static bool\ndes3_cbc_crypt(", "static void handle_ulc_auth_start(void) {",
         "static void handle_ulc_auth_step2(const uint8_t *data, int len) {"]
code = "\n".join(extract(source[source.rindex(n):], n) for n in names)
constants = "\n".join(l for l in source.splitlines() if l.startswith("#define T2T_"))
fixture = r"""
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "des.h"
/* CONSTANTS */
typedef struct { bool encrypt; const uint8_t *key, *iv_in; uint8_t *iv_out; } des3_cbc_params_t;
enum { ULC_AUTH_IDLE, ULC_AUTH_WAIT };
static struct { bool supports_ulc_auth; } s_prof = {true};
static bool s_ulc_auth_enabled=true, s_authenticated;
static int s_ulc_state;
static uint8_t s_ulc_key[16]={0x49,0x45,0x4D,0x4B,0x41,0x45,0x52,0x42,0x21,0x4E,0x41,0x43,0x55,0x4F,0x59,0x46};
static uint8_t s_ulc_rndb[8], s_ulc_last_iv[8], reply[32];
static int reply_length;
static void ulc_random(uint8_t *out,size_t len) {
  const uint8_t rnd[]={0x51,0xE7,0x64,0x60,0x26,0x78,0xDF,0x2B};
  assert(len==sizeof(rnd));memcpy(out,rnd,len);
}
static void tx_with_crc(const uint8_t *p,int n) {memcpy(reply,p,n);reply_length=n;}
static void tx_4bit_nack(uint8_t code) {(void)code;reply_length=-1;}
/* CODE */
int main(void) {
  const uint8_t first[]={0xAF,0x57,0x72,0x93,0xFD,0x2F,0x34,0xCA,0x51};
  uint8_t request[]={0xAF,0x0A,0x63,0x85,0x59,0xFC,0x77,0x37,0xF9,0xF1,0x5D,0x78,0x62,0xEB,0xBE,0x96,0x7A};
  const uint8_t last[]={0,0x3B,0x88,0x4F,0xA0,0x7C,0x13,0x7C,0xE1};
  s_authenticated=true;handle_ulc_auth_start();assert(!s_authenticated);
  assert(reply_length==9 && !memcmp(reply,first,9));
  handle_ulc_auth_step2(request,sizeof(request));assert(s_authenticated);
  assert(reply_length==9 && !memcmp(reply,last,9));
  handle_ulc_auth_start();request[16]^=1;
  handle_ulc_auth_step2(request,sizeof(request));
  assert(!s_authenticated && reply_length==-1 && s_ulc_state==ULC_AUTH_IDLE);
  s_authenticated=true;handle_ulc_auth_step2(request,1);assert(!s_authenticated);
  s_ulc_auth_enabled=false;s_authenticated=true;handle_ulc_auth_start();
  assert(!s_authenticated && reply_length==-1);
}
"""
with tempfile.TemporaryDirectory(prefix="ulc-auth-") as tmp:
    p=Path(tmp)
    (p/"test.c").write_text(fixture.replace("/* CONSTANTS */",constants).replace("/* CODE */",code))
    des=BASE/"common/legacy_des"
    subprocess.run(["cc","-std=c11","-Wall","-Wextra","-Werror",
                    "-fsanitize=address,undefined","-I"+str(des),str(p/"test.c"),
                    str(des/"des.c"),"-o",str(p/"test")],check=True)
    subprocess.run([str(p/"test")],check=True)
print("UL-C authentication: NXP vector, failed challenge, short frame and unknown key passed")
