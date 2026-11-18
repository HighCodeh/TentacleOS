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

"""Exercise production menu controls and ownership against explicit UI/service fakes."""
from pathlib import Path
import re,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[2]
def extract(s,sig):
 a=s.index(sig);b=s.index('{',a)+1;depth=1
 while depth:
  depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
def main():
 s=(ROOT/'firmware_p4/components/Applications/ui/screens/lora/lora_rnode_ui.c').read_text()
 defs='\n'.join(re.findall(r'^#define .*$',s,re.M))
 arrays='\n'.join(re.findall(r'^static const (?:int8_t|uint32_t)\b[^;]*;',s,re.M))
 enum=s[s.index('typedef enum {'):s.index('} rf_lab_field_t;')+len('} rf_lab_field_t;')]
 enum+='\n'+s[re.search(r'typedef\s+struct\s*\{\s*const char \*name;', s).start():s.index('static const char *BRANCHES')]
 enum+='\nstatic uint8_t s_preset;'
 native='\n'.join(extract(s,x) for x in ['static uint32_t cycle(', 'static void normalize(void) {','static void adjust(', 'static void stop_owned(void) {', 'void ui_lora_rnode_close(void) {','static void start(void) {'])
 fixture=r"""
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "rf_chamber.h"
/* DEFINITIONS */
static rf_chamber_config_t s_config;
static unsigned s_owned_run,starts,stops,errors,builds;
static void *s_timer=(void*)1;
static unsigned s_step;
static rf_chamber_status_t snapshot;
#define LORA_PROTO_NONE 0
#define NOTIFY_WARNING 0
static int mesh;
static void lv_timer_delete(void *p){assert(p!=NULL);}
static int lora_session_active(void){return mesh;}
static void notify(int kind,const char *text){(void)kind;(void)text;errors++;}
static void show_error(esp_err_t error){assert(error!=ESP_OK);errors++;}
static void build(bool live){assert(live);builds++;}
void rf_chamber_snapshot(rf_chamber_status_t *out){*out=snapshot;}
esp_err_t rf_chamber_start(const rf_chamber_config_t *c){assert(rf_chamber_validate(c)==ESP_OK);starts++;snapshot.run_id++;snapshot.phase=RF_CHAMBER_RUNNING;return ESP_OK;}
void rf_chamber_stop(void){stops++;}
/* NATIVE */
int main(void){
 s_config=(rf_chamber_config_t){.radio=RF_CHAMBER_CC,.mode=RF_CHAMBER_RX,.frequency_hz=433920000,.power_dbm=-30,.path=1,.sf=7,.bandwidth_khz=125,.cr=1,.duration_ms=5000,.interval_ms=1000};
 for(unsigned i=1;i<PRESET_COUNT;i++){adjust(FIELD_QUICK,1,false);assert(rf_chamber_validate(&s_config)==ESP_OK);assert(starts==0);assert(s_config.power_dbm<=-9);}
 s_preset=0;s_config=(rf_chamber_config_t){.radio=RF_CHAMBER_CC,.mode=RF_CHAMBER_RX,.frequency_hz=433920000,.power_dbm=-30,.path=1,.sf=7,.bandwidth_khz=125,.cr=1,.duration_ms=5000,.interval_ms=1000};
 for(int radio=0;radio<2;radio++){
  for(int mode=0;mode<3;mode++){
   s_config.mode=mode;
   for(int power=0;power<(radio?32:6);power++){
    adjust(FIELD_POWER,1,false);normalize();assert(rf_chamber_validate(&s_config)==ESP_OK);
    if(radio && mode==RF_CHAMBER_CW && s_config.power_dbm>14)assert(s_config.duration_ms<=1000);
   }
   for(int n=0;n<8;n++){adjust(FIELD_DURATION,1,false);assert(rf_chamber_validate(&s_config)==ESP_OK);}
   for(int n=0;n<8;n++){adjust(FIELD_FREQUENCY,1,true);assert(rf_chamber_validate(&s_config)==ESP_OK);}
  }
  adjust(FIELD_RADIO,1,false);assert(rf_chamber_validate(&s_config)==ESP_OK);
 }
 s_config.mode=RF_CHAMBER_RX;s_config.radio=RF_CHAMBER_CC;s_config.frequency_hz=300000000;s_config.path=1;s_config.power_dbm=-30;
 s_step=3;adjust(FIELD_FREQUENCY,-1,false);assert(s_config.frequency_hz==300000000);
 snapshot=(rf_chamber_status_t){.run_id=10,.phase=RF_CHAMBER_RUNNING};s_owned_run=9;stop_owned();assert(stops==0);
 s_owned_run=10;stop_owned();assert(stops==1);start();assert(starts==0 && errors==1);
 snapshot.phase=RF_CHAMBER_IDLE;s_timer=NULL;start();assert(starts==0 && errors==2);
 s_timer=(void*)1;s_config.radio=RF_CHAMBER_SX;mesh=1;start();assert(starts==0 && errors==3);
 mesh=0;s_config.frequency_hz=868100000;s_config.path=0;s_config.power_dbm=-9;start();assert(starts==1 && builds==1 && s_owned_run==11);
 ui_lora_rnode_close();assert(stops==2 && s_timer==NULL);snapshot.phase=RF_CHAMBER_IDLE;ui_lora_rnode_close();assert(stops==2);
 puts("PASS: menu frequency boundaries, every power, CW limits, no competing starts, mesh gate, timer failure, own-run stop and idempotent close");
}
"""
 with tempfile.TemporaryDirectory() as tmp:
  d=Path(tmp);(d/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n#define ESP_ERR_INVALID_STATE 2\n#define ESP_ERR_NO_MEM 3\n')
  (d/'esp_log.h').write_text('#define ESP_LOGE(tag,...) ((void)(tag))\n')
  (d/'test.c').write_text(fixture.replace('/* DEFINITIONS */',defs+'\n'+arrays+'\n'+enum).replace('/* NATIVE */',native))
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(d),'-I'+str(ROOT/'firmware_p4/components/Service/rf_chamber/include'),str(d/'test.c'),str(ROOT/'firmware_p4/components/Service/rf_chamber/rf_chamber_config.c'),'-o',str(d/'test')],check=True)
  subprocess.run([str(d/'test')],check=True)
if __name__=='__main__':main()
