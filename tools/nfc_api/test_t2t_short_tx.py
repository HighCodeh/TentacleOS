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


"""Verify Type 2 nibble replies use the passive bit transmitter and fail closed."""
from pathlib import Path
import os, shlex, subprocess, tempfile
from test_emu_tx import extract
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'firmware_p4/components/Applications/nfc/protocols/t2t/t2t_emu.c').read_text()
signature='static void tx_4bit_nack(uint8_t code) {'
code=extract(source[source.rindex(signature):],signature)
fixture=r"""
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#define HB_NFC_OK 0
#define T2T_ACK 10
#define T2T_NIBBLE_BITS 4
#define T2T_NIBBLE_MASK 15
static bool s_transport_error, aes;
static unsigned calls, resets;
static uint8_t value;
static size_t count;
static int failure;
static bool is_aes(void) {return aes;}
static void t2t_reset_auth(void) {resets++;}
static int hb_nfc_spi_target_tx_bits(const uint8_t *data,size_t bits) {
 calls++;value=*data;count=bits;return failure;
}
/* PRODUCTION */
int main(void) {
 for(unsigned code=0;code<256;code++) {
  tx_4bit_nack((uint8_t)code);
  assert(calls==code+1 && value==(code&15) && count==4 && !s_transport_error);
 }
 assert(!resets);
 aes=true;tx_4bit_nack(T2T_ACK);assert(!resets);
 tx_4bit_nack(0);assert(resets==1);
 failure=1;tx_4bit_nack(4);assert(s_transport_error && resets==2);
 return 0;
}
"""
with tempfile.TemporaryDirectory(prefix='t2t-short-tx-') as temp:
 path=Path(temp);(path/'test.c').write_text(fixture.replace('/* PRODUCTION */',code))
 subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(path/'test.c'),'-o',str(path/'test')],check=True)
 subprocess.run([str(path/'test')],check=True,timeout=20)
print('PASS: four-bit passive ACK/NACK, nibble masking, AES authentication reset and TX error propagation')
