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


"""Exercise the real bounded voltage diagnostic with explicit ADC/ownership fakes."""
import os,re,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
DRIVER=ROOT/'firmware_p4/components/Drivers/lf_rfid'
source=re.sub(r'^#include .*$', '', (DRIVER/'lf_rfid_voltage.c').read_text(), flags=re.M)
prelude=r"""
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "lf_rfid.h"
#define RF_FRONTEND_LF_125KHZ 1
#define GPIO_RFID_LF_DATA_PIN 16
#define ADC_ATTEN_DB_12 3
#define ADC_BITWIDTH_DEFAULT 12
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
#define pdMS_TO_TICKS(x) (x)
typedef int adc_unit_t;
typedef int adc_channel_t;
typedef void *adc_oneshot_unit_handle_t;
typedef void *adc_cali_handle_t;
typedef struct {int unit_id;} adc_oneshot_unit_init_cfg_t;
typedef struct {int atten,bitwidth;} adc_oneshot_chan_cfg_t;
typedef struct {int unit_id,chan,atten,bitwidth;} adc_cali_curve_fitting_config_t;
typedef struct {uint64_t pin_bit_mask;int mode,pull_up_en,pull_down_en,intr_type;} gpio_config_t;
static bool reserved,field,external,analog,adc_live,cal_live,uncal;
static int failure,stop_calls,reads,starts,restores;
static int64_t now;
static int rf_frontend_reserve(int mode){assert(mode==1);if(reserved)return ESP_ERR_INVALID_STATE;reserved=true;return 0;}
static int rf_frontend_unreserve(int mode){assert(mode==1&&reserved&&!field);if(failure==14)return ESP_FAIL;reserved=false;return 0;}
int lf_rfid_init(void){assert(reserved);return failure==1?ESP_FAIL:0;}
int lf_rfid_field_stop_checked(void){assert(reserved);stop_calls++;if(failure==12&&stop_calls>1)return ESP_FAIL;field=false;return 0;}
int lf_rfid_listen_external(uint32_t ms,lf_rfid_field_report_t *r){assert(!field&&reserved&&ms==30);r->carrier_present=external;return failure==2?ESP_FAIL:0;}
int lf_rfid_field_start(uint32_t hz,uint16_t duty){assert(reserved&&!external&&!field&&hz==125000&&duty==500);if(failure==3)return ESP_FAIL;field=true;starts++;return 0;}
static int adc_oneshot_io_to_channel(int pin,adc_unit_t *u,adc_channel_t *c){assert(pin==16);*u=0;*c=0;return failure==4?ESP_FAIL:0;}
static int adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *c,void **h){assert(c->unit_id==0&&!adc_live);if(failure==5)return ESP_FAIL;adc_live=true;*h=(void*)1;return 0;}
static int adc_oneshot_config_channel(void *h,int ch,const adc_oneshot_chan_cfg_t *c){assert(h&&ch==0&&c->atten==3&&c->bitwidth==12);analog=true;return failure==6?ESP_FAIL:0;}
static int adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t *c,void **h){assert(c->unit_id==0&&c->chan==0&&c->atten==3);if(uncal)return ESP_ERR_NOT_SUPPORTED;if(failure==7)return ESP_FAIL;cal_live=true;*h=(void*)2;return 0;}
static int adc_oneshot_read(void *h,int ch,int *raw){assert(h&&ch==0&&reserved&&analog);reads++;now+=50;*raw=reads%2?200:1250;return failure==8?ESP_FAIL:0;}
static int adc_cali_raw_to_voltage(void *h,int raw,int *mv){assert(h&&cal_live);*mv=raw;return failure==9?ESP_FAIL:0;}
static int adc_cali_delete_scheme_curve_fitting(void *h){assert(h&&cal_live);if(failure==10)return ESP_FAIL;cal_live=false;return 0;}
static int adc_oneshot_del_unit(void *h){assert(h&&adc_live);if(failure==11)return ESP_FAIL;adc_live=false;return 0;}
static int gpio_config(const gpio_config_t *c){assert((!field||failure==12)&&c->pin_bit_mask==(1ULL<<16)&&c->mode==1&&!c->pull_up_en&&!c->pull_down_en&&!c->intr_type);restores++;if(failure==13)return ESP_FAIL;analog=false;return 0;}
static int64_t esp_timer_get_time(void){return now;}
static void vTaskDelay(int ms){now+=(int64_t)ms*1000;}
static void reset(void){reserved=field=external=analog=adc_live=cal_live=uncal=false;failure=stop_calls=reads=starts=restores=0;now=0;}
"""
main=r"""
int main(void){
 lf_rfid_voltage_report_t r;
 for(int active=0;active<2;active++){
  reset();assert(lf_rfid_measure_voltage(active,100,&r)==0);
  assert(r.samples==(unsigned)reads&&r.samples>0&&r.raw_min==200&&r.raw_max==1250);
  assert(r.raw_mean>=200&&r.raw_mean<=1250&&r.calibrated&&r.min_mv==200&&r.max_mv==1250);
  assert(!reserved&&!field&&!adc_live&&!cal_live&&!analog&&!r.requires_reset&&restores==1&&starts==active);
  assert(now>=100000+(active?450000:0)&&now<102000+(active?450000:0));
 }
 reset();external=true;assert(lf_rfid_measure_voltage(true,100,&r)==ESP_ERR_INVALID_STATE);assert(!reserved&&!field&&!starts&&!reads);
 reset();external=true;assert(lf_rfid_measure_voltage(false,100,&r)==0);assert(!starts&&reads>0);
 reset();uncal=true;assert(lf_rfid_measure_voltage(true,100,&r)==0);assert(!r.calibrated&&r.samples&&!reserved&&!analog);
 for(int f=1;f<=14;f++){
  reset();failure=f;assert(lf_rfid_measure_voltage(true,100,&r)!=0);
  if(f>=10){assert(r.requires_reset&&reserved);}
  else {assert(!r.requires_reset&&!reserved&&!field&&!adc_live&&!cal_live&&!analog);}
  if(f==9)assert(!r.calibrated);
 }
 reset();reserved=true;assert(lf_rfid_measure_voltage(true,100,&r)==ESP_ERR_INVALID_STATE);assert(reserved&&!stop_calls&&!starts&&!reads);
 reset();assert(lf_rfid_measure_voltage(true,99,&r)==ESP_ERR_INVALID_ARG);assert(lf_rfid_measure_voltage(true,1001,&r)==ESP_ERR_INVALID_ARG);assert(lf_rfid_measure_voltage(true,100,NULL)==ESP_ERR_INVALID_ARG);assert(!reserved&&!stop_calls);
 puts("LF voltage: calibrated/raw, boundaries, busy/external refusal and 14 failure paths PASS");
}
"""
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)
 errors=['ESP_OK','ESP_FAIL','ESP_ERR_INVALID_ARG','ESP_ERR_INVALID_STATE','ESP_ERR_NOT_FOUND','ESP_ERR_NOT_SUPPORTED']
 (p/'esp_err.h').write_text('#pragma once\ntypedef int esp_err_t;\n'+''.join('#define '+v+' '+str(i)+'\n' for i,v in enumerate(errors)))
 (p/'test.c').write_text(prelude+source+main)
 subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all','-I'+str(p),'-I'+str(DRIVER/'include'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True,timeout=20)
