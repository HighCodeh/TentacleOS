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

"""Check old NFC records against the production decoder without writing NVS."""
import re, subprocess, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
types=(root/'firmware_p4/components/Drivers/st25r3916/include/highboy_nfc_types.h').read_text()
# Frozen previous layout, independent of the decoder's legacy declaration.
legacy=re.sub(r'typedef struct \{\s*uint8_t ats\[.*?\} highboy_nfc_iso_dep_t;', 'typedef struct {\n  uint8_t ats[64]; uint16_t ats_len; uint8_t fsc; uint8_t fwi;\n} highboy_nfc_iso_dep_t;', types, flags=re.S)
legacy=legacy.replace('highboy_', 'v1_highboy_').replace('HIGHBOY_', 'V1_HIGHBOY_')
source=(root/'firmware_p4/components/Applications/nfc/nfc_device.c').read_text()
start=source.index('/* Generic descriptors saved before FSC')
end=source.index('hb_nfc_err_t nfc_device_load_generic(',start)
alias='\ntypedef highboy_nfc_card_t hb_nfc_card_data_t;\n'
for name in ('ISO14443_4A','MF_PLUS','MF_DESFIRE'):
    alias+=f'#define HB_PROTO_{name} HIGHBOY_NFC_PROTO_{name}\n'
test=r"""
#include <assert.h>
int main(void) {
  _Static_assert(sizeof(v1_highboy_nfc_card_t)==96,"Old wire ABI");
  _Static_assert(sizeof(highboy_nfc_card_t)==100,"Current wire ABI");
  unsigned checks=0;
  for (int p=0;p<V1_HIGHBOY_NFC_PROTO_COUNT;p++) {
    v1_highboy_nfc_card_t old={0};
    old.protocol=(v1_highboy_nfc_protocol_t)p;
    old.iso14443a.uid_len=7;
    memcpy(old.iso14443a.uid,"ABCDEFG",7);
    old.iso14443a.atqa[0]=0x44; old.iso14443a.sak=0x20;
    for (size_t i=0;i<sizeof(old.data);i++) ((uint8_t*)&old.data)[i]=(uint8_t)i;
    old.data.iso_dep.ats_len=2; old.data.iso_dep.fsc=255; old.data.iso_dep.fwi=8;
    old.rf.amplitude=83; old.rf.regulator_mv=3100; old.rf.valid=true;
    uint8_t unaligned[sizeof(old)+1]; memcpy(unaligned+1,&old,sizeof(old));
    highboy_nfc_card_t out;
    assert(decode_generic_payload(unaligned+1,sizeof(old),&out));checks++;
    assert(out.protocol==(highboy_nfc_protocol_t)p);checks++;
    assert(memcmp(&out.iso14443a,&old.iso14443a,sizeof(old.iso14443a))==0);checks++;
    assert(memcmp(&out.rf,&old.rf,sizeof(old.rf))==0);checks++;
    if (p==V1_HIGHBOY_NFC_PROTO_ISO14443_4A || p==V1_HIGHBOY_NFC_PROTO_MF_PLUS || p==V1_HIGHBOY_NFC_PROTO_MF_DESFIRE) {
      assert(out.data.iso_dep.fsc==256 && out.data.iso_dep.fsd==256);checks++;
      assert(out.data.iso_dep.fwi==8 && out.data.iso_dep.cid==0);checks++;
      assert(out.data.iso_dep.ats_len==2 && memcmp(out.data.iso_dep.ats,old.data.iso_dep.ats,64)==0);checks++;
    } else { assert(memcmp(&out.data,&old.data,sizeof(old.data))==0);checks++; }
    highboy_nfc_card_t again;
    assert(decode_generic_payload((const uint8_t*)&out,sizeof(out),&again));checks++;
    assert(memcmp(&out,&again,sizeof(out))==0);checks++;
  }
  for (size_t n=0;n<120;n++) {
    uint8_t payload[120]={0}; highboy_nfc_card_t out;
    assert(decode_generic_payload(payload,n,&out)==(n==96 || n==100));checks++;
  }
  printf("PASS generic NFC storage: %u assertions, 12 legacy protocols, unaligned records and current roundtrip\n",checks);
}
"""
with tempfile.TemporaryDirectory() as d:
    exe=str(Path(d)/'storage')
    program='#include <stddef.h>\n#include <stdio.h>\n#include <string.h>\n'+types+legacy+alias+source[start:end]+test
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined,bounds','-fno-sanitize-recover=all','-x','c','-','-o',exe],input=program,text=True,check=True)
    subprocess.run([exe],check=True)
