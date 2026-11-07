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

"""Verify Type 2 geometry and bounded reads of production page reader."""
from pathlib import Path
import subprocess
import tempfile
from test_emu_tx import extract
from test_readers import defines
ROOT=Path(__file__).resolve().parents[2]
source=(ROOT/'firmware_p4/components/Applications/nfc/protocols/mifare/mf_ultralight.c').read_text()
code="\n".join(extract(source,n) for n in ('uint16_t mful_version_pages(', 'int mful_read_all('))
fixture=r"""
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
/* CONSTANTS */
static unsigned calls, fail_page=256;
static int mful_read_pages(uint8_t page,uint8_t out[18]) {
  calls++;
  if(page>=fail_page) return 0;
  for(unsigned i=0;i<16;i++) out[i]=page+i/4;
  return 18;
}
/* CODE */
int main(void) {
  uint8_t version[8]={0,4,4,2,1,0,0,3};
  const uint8_t sizes[]={0x0B,0x0E,0x0F,0x11,0x13};
  const unsigned pages[]={20,41,45,135,231};
  uint8_t memory[1024];
  for(unsigned i=0;i<5;i++) {
    version[6]=sizes[i];assert(mful_version_pages(version)==pages[i]);
    memset(memory,0xAA,sizeof(memory));calls=0;
    assert(mful_read_all(memory,pages[i])==(int)pages[i]);
    assert(calls==(pages[i]+3)/4 && memory[pages[i]*4]==0xAA);
    assert(memory[(pages[i]-1)*4]==pages[i]-1);
  }
  version[3]=5;assert(mful_version_pages(version)==0);
  version[3]=2;version[2]=3;assert(mful_version_pages(version)==0);
  version[6]=0x0E;assert(mful_version_pages(version)==41);
  version[6]=0x0B;assert(mful_version_pages(version)==20);
  version[1]=0;assert(mful_version_pages(version)==0);
  assert(mful_version_pages(NULL)==0);
  calls=0;assert(mful_read_all(memory,257)==0 && calls==0);
  fail_page=8;assert(mful_read_all(memory,45)==8);
}
"""
with tempfile.TemporaryDirectory(prefix='t2t-reader-') as tmp:
    p=Path(tmp);(p/'test.c').write_text(fixture.replace('/* CONSTANTS */',defines(source)).replace('/* CODE */',code))
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('Type 2 reader: geometry, no address wrapping, partial reads and output bounds passed')
