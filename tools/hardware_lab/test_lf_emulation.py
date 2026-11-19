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


"""Exercise the real EM4100 encoder and bounded RMT lifecycle with host fakes."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
import lab

class EmulationTest(unittest.TestCase):
    def test_frames_timing_ownership_and_failures(self):
        base = lab.ROOT/'firmware_p4/components/Drivers/lf_rfid'
        source = (base/'lf_rfid.c').read_text()
        header = (base/'include/lf_rfid.h').read_text()
        code = source[re.search(r'static void\s+encode_em4100\(', source).start():source.index('esp_err_t lf_rfid_self_test(')]
        defines = '\n'.join(re.findall(r'^#define LF_(?:EM_\w+|FRAME_BITS)\s+[^\n]+', source, re.M))
        prelude = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "lf_rfid.h"
#define RF_FRONTEND_LF_125KHZ 1
#define LF_EXTERNAL_CHECK_MS 30
#define LF_LEDC_MODE 0
#define LF_LEDC_CHANNEL 7
#define GPIO_RFID_LF_MOD_PIN 11
#define GPIO_MODE_OUTPUT 1
#define RMT_CLK_SRC_DEFAULT 0
#define SOC_RMT_MEM_WORDS_PER_CHANNEL 48
#define DRAM_ATTR
#define ESP_ERR_TIMEOUT 5
#define ESP_ERR_NO_MEM 6
#define ESP_FAIL 7

typedef struct {uint32_t duration0:15, level0:1, duration1:15, level1:1;} rmt_symbol_word_t;
typedef void *rmt_channel_handle_t;
typedef void *rmt_encoder_handle_t;
typedef struct {int gpio_num,clk_src,resolution_hz,mem_block_symbols,trans_queue_depth;} rmt_tx_channel_config_t;
typedef struct {int unused;} rmt_copy_encoder_config_t;
typedef struct {int loop_count;struct {int eot_level;} flags;} rmt_transmit_config_t;
static rmt_symbol_word_t s_emulation_symbols[64];
static bool reserved,claimed,enabled,allocated,encoding,restored,external;
static int failure,timeout_ms,loops,steps;
static uint32_t carrier_hz;
static int step(int id){steps++;return failure==id?ESP_FAIL:ESP_OK;}
static int rf_frontend_reserve(int m){assert(m==1);if(reserved)return ESP_ERR_INVALID_STATE;reserved=true;return 0;}
int lf_rfid_init(void){return step(1);}
int lf_rfid_listen_external(uint32_t ms,lf_rfid_field_report_t *r){assert(reserved&&ms==30);r->carrier_present=external;r->measured_hz=carrier_hz;return step(2);}
static int rf_frontend_claim(int m){assert(m==1&&reserved);if(step(3))return ESP_FAIL;claimed=true;return 0;}
static int rmt_new_tx_channel(const rmt_tx_channel_config_t *c,void **h){assert(claimed&&c->gpio_num==11&&c->resolution_hz==1000000&&c->mem_block_symbols>=65&&c->trans_queue_depth==1);if(step(4))return ESP_FAIL;allocated=true;*h=(void*)1;return 0;}
static int rmt_new_copy_encoder(const rmt_copy_encoder_config_t *c,void **h){(void)c;assert(allocated);if(step(5))return ESP_FAIL;encoding=true;*h=(void*)2;return 0;}
static int rmt_enable(void *c){assert(c&&allocated);if(step(6))return ESP_FAIL;enabled=true;return 0;}
static int rmt_transmit(void *c,void *e,const void *data,size_t size,const rmt_transmit_config_t *t){assert(c&&e&&enabled&&data==s_emulation_symbols&&size==256&&t->loop_count>0&&t->flags.eot_level==0);loops=t->loop_count;return step(7);}
static int rmt_tx_wait_all_done(void *c,int ms){assert(c&&enabled);timeout_ms=ms;return step(8);}
static int rmt_disable(void *c){assert(c&&enabled);if(step(9))return ESP_FAIL;enabled=false;return 0;}
static int rmt_del_channel(void *c){assert(c&&allocated&&!enabled);if(step(10))return ESP_FAIL;allocated=false;return 0;}
static int rmt_del_encoder(void *e){assert(e&&encoding&&!enabled&&!allocated);if(step(11))return ESP_FAIL;encoding=false;return 0;}
static int gpio_set_level(int p,int level){assert(p==11&&!level&&!enabled);return step(12);}
static int gpio_set_direction(int p,int dir){assert(p==11&&dir==1&&!allocated);return step(13);}
static int ledc_set_duty(int m,int c,int duty){assert(!m&&c==7&&!duty);return step(14);}
static int ledc_update_duty(int m,int c){assert(!m&&c==7);return step(15);}
static int ledc_set_pin(int p,int m,int c){assert(p==11&&!m&&c==7);if(step(16))return ESP_FAIL;restored=true;return 0;}
static void rf_frontend_release(int m){assert(m==1&&!enabled);claimed=false;}
static int rf_frontend_unreserve(int m){assert(m==1&&!claimed);if(step(17))return ESP_FAIL;reserved=false;return 0;}
static void reset(void){reserved=claimed=enabled=allocated=encoding=restored=false;external=true;carrier_hz=125000;failure=timeout_ms=loops=steps=0;}
"""
        main = r"""
int main(void){
 const uint8_t vectors[][5]={{1,2,3,4,5},{0,0,0,0,0},{255,255,255,255,255},{0x5a,0x91,0x27,0xcb,0x80}};
 const uint64_t expected[]={0xff8060280c048142ULL,0xff80000000000000ULL,0xfffbdef7bdef7bc0ULL,0xffaa92195f8bc41aULL};
 lf_rfid_emulation_report_t report;
 for(size_t v=0;v<4;v++)for(int invert=0;invert<2;invert++){
  reset();assert(lf_rfid_emulate_em4100(vectors[v],3000,invert,&report)==ESP_OK);
  assert(!reserved&&!claimed&&!enabled&&!allocated&&!encoding&&restored&&!report.requires_reset);
  assert(report.half_bit_us==256&&report.frame_count==91&&report.duration_us==2981888&&loops==91&&timeout_ms==3300);
  uint64_t frame=0;
  for(int bit=0;bit<64;bit++){
   rmt_symbol_word_t s=s_emulation_symbols[bit];
   assert(s.duration0==256&&s.duration1==256&&s.level0!=s.level1);
   frame=(frame<<1)|(s.level0^invert);
  }
  assert(frame==expected[v]);
 }
 for(int fail=1;fail<=17;fail++){
  reset();failure=fail;assert(lf_rfid_emulate_em4100(vectors[0],3000,false,&report)!=ESP_OK);
  if(fail>=9){assert(reserved&&report.requires_reset);int before=steps;assert(lf_rfid_emulate_em4100(vectors[0],3000,false,&report)==ESP_ERR_INVALID_STATE&&steps==before);}
  else {assert(!reserved&&!claimed&&!enabled&&!allocated&&!encoding&&!report.requires_reset);if(fail>=4)assert(restored);}
 }
 reset();external=false;assert(lf_rfid_emulate_em4100(vectors[0],3000,false,&report)==ESP_ERR_NOT_FOUND);assert(!reserved&&!allocated&&steps==3);
 reset();reserved=true;assert(lf_rfid_emulate_em4100(vectors[0],3000,false,&report)==ESP_ERR_INVALID_STATE&&steps==0);
 reset();assert(lf_rfid_emulate_em4100(NULL,3000,false,&report)==ESP_ERR_INVALID_ARG);assert(lf_rfid_emulate_em4100(vectors[0],3000,false,NULL)==ESP_ERR_INVALID_ARG);
 assert(lf_rfid_emulate_em4100(vectors[0],99,false,&report)==ESP_ERR_INVALID_ARG);
 assert(lf_rfid_emulate_em4100(vectors[0],5001,false,&report)==ESP_ERR_INVALID_ARG&&steps==0);
 for(uint32_t hz=110000;hz<=140000;hz+=1000)for(uint32_t ms=100;ms<=5000;ms+=100){
  reset();carrier_hz=hz;assert(lf_rfid_emulate_em4100(vectors[0],ms,false,&report)==ESP_OK);
  assert(report.duration_us<=ms*1000&&report.frame_count>=1&&report.half_bit_us>=229&&report.half_bit_us<=291);
 }
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)
            (path/'lf_rfid.h').write_text(header)
            (path/'esp_err.h').write_text('\n'.join('// '+line for line in lab.NOTICE.splitlines())+'\n'+'typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_STATE 1\n#define ESP_ERR_INVALID_ARG 2\n#define ESP_ERR_NOT_FOUND 3\n')
            (path/'test.c').write_text('\n'.join('// '+line for line in lab.NOTICE.splitlines())+'\n'+prelude+defines+'\n'+code+main)
            subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I'+tmp,str(path/'test.c'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)

if __name__=='__main__':unittest.main()
