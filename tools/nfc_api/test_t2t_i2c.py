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

"""Check I2C page mapping, sector handshakes and bounded production reads."""
from pathlib import Path
import subprocess
import tempfile
from test_emu_tx import extract
from test_readers import defines
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'firmware_p4/components/Applications/nfc/protocols/mifare/mf_ultralight.c').read_text()
code="\n".join(extract(source,n) for n in ('uint16_t mful_i2c_pages(', 'static bool i2c_address(',
    'static bool select_sector(', 'int mful_read_i2c('))
fixture=r"""
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
/* CONSTANTS */
static uint8_t selected;
static bool fail_ack,fail_passive,fail_restore;
static unsigned reads,fail_read=1000;
static int nfc_poller_transceive(const uint8_t *tx,size_t n,bool crc,uint8_t *rx,size_t max,size_t min,int timeout) {
  assert(n==2 && tx[0]==0xC2 && tx[1]==255 && crc && max>=1 && min==1 && timeout>=1);
  rx[0]=fail_ack?0:0x0A;return 1;
}
static bool nfc_poller_expect_silence(const uint8_t *tx,size_t n,int timeout) {
  assert(n==4 && tx[1]==0 && tx[2]==0 && tx[3]==0 && timeout>=1);
  if(fail_passive || (fail_restore && tx[0]==0 && selected!=0)) return false;
  selected=tx[0];return true;
}
static int mful_read_pages(uint8_t page,uint8_t out[18]) {
  if(reads++==fail_read) return 0;
  for(unsigned i=0;i<4;i++) {out[i*4]=selected;out[i*4+1]=page+i;out[i*4+2]=0;out[i*4+3]=0;}
  return 18;
}
/* CODE */
int main(void) {
  uint8_t version[8]={0,4,4,5,2,0,0x13,3};
  const unsigned totals[]={231,485,236,492};
  uint8_t memory[492*4+1];
  for(unsigned model=0;model<4;model++) {
    version[5]=1+model/2;version[6]=(model%2)?0x15:0x13;
    assert(mful_i2c_pages(version)==totals[model]);
    reads=0;memset(memory,0xAA,sizeof(memory));
    assert(mful_read_i2c(version,memory,sizeof(memory))==(int)totals[model]);
    assert(selected==0 && memory[totals[model]*4]==0xAA);
    for(unsigned i=0;i<totals[model];i++) {
      unsigned sec=0,page=i;
      if(model==0 && i>=227) {sec=i>=229?3:0;page=(i>=229?248:232)+(i-(i>=229?229:227));}
      if(model==1) {
        if(i>=483) {sec=3;page=248+i-483;}
        else if(i>=481) {sec=1;page=232+i-481;}
        else {sec=i/256;page=i%256;}
      }
      if(model>=2 && i>=234) {
        if(i<236) page=236+i-234;
        else {sec=1;page=i-236;}
      }
      assert(memory[i*4]==sec && memory[i*4+1]==page);
    }
    uint8_t sector,page;uint16_t left;
    assert(!i2c_address(totals[model],totals[model],&sector,&page,&left));
  }
  reads=0;assert(mful_read_i2c(version,memory,100)==0 && reads==0);
  fail_ack=true;assert(mful_read_i2c(version,memory,sizeof(memory))==0 && reads==0);
  fail_ack=false;fail_passive=true;assert(mful_read_i2c(version,memory,sizeof(memory))==0);
  fail_passive=false;reads=0;fail_read=2;assert(mful_read_i2c(version,memory,sizeof(memory))==8);
  fail_read=1000;reads=0;fail_restore=true;assert(mful_read_i2c(version,memory,sizeof(memory))==0);
  version[2]=7;assert(mful_i2c_pages(version)==492);
  version[5]=0;assert(mful_i2c_pages(version)==0);
  version[5]=3;assert(mful_i2c_pages(version)==0);assert(mful_i2c_pages(NULL)==0);
}
"""
with tempfile.TemporaryDirectory(prefix='t2t-i2c-') as tmp:
    p=Path(tmp);(p/'test.c').write_text(fixture.replace('/* CONSTANTS */',defines(source)).replace('/* CODE */',code))
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('NTAG I2C: four maps, every page, handshake failures, partial reads and bounds passed')
