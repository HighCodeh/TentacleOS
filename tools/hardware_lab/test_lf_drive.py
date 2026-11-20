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

"""Exercise actual carrier/load functions with independent GPIO waveform models."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
import lab


class DriveTest(unittest.TestCase):
    def test_reader_uses_gpio2_and_load_test_uses_gpio11(self):
        source = (lab.ROOT/'firmware_p4/components/Drivers/lf_rfid/lf_rfid.c').read_text()
        constants = '\n'.join(re.findall(r'^#define LF_(?:LEDC_\w+|MOD_\w+|EXTERNAL_CHECK_MS)\s+[^\n]+', source, re.M))
        channel = source[source.index('  ledc_channel_config_t channel = {'):source.index('  esp_err_t err = gpio_isr_handler_add')]
        field = source[source.index('static esp_err_t start_field('):source.index('static esp_err_t measure_feedback(')]
        load = source[source.index('esp_err_t lf_rfid_listen_external('):source.index('esp_err_t lf_rfid_capture_edges(')]
        prelude = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_INVALID_ARG 3
#define ESP_ERR_NOT_FOUND 4
#define RF_FRONTEND_LF_125KHZ 1
#define RF_FRONTEND_IDLE 0
#define GPIO_RFID_LF_COIL_PIN 2
#define GPIO_RFID_LF_MOD_PIN 11
#define LF_RFID_NOMINAL_HZ 125000
#define LF_RFID_MIN_HZ 110000
#define LF_RFID_MAX_HZ 140000
#define LF_RFID_DEFAULT_DUTY_PERMILLE 500
#define LF_RFID_SCOPE_MIN_HZ 20000
#define LF_RFID_SCOPE_MAX_HZ 200000
#define LF_RFID_SCOPE_MIN_MS 10
#define LF_RFID_SCOPE_MAX_MS 5000
#define LF_MEASURE_MIN_MS 10
#define LF_MEASURE_MAX_MS 1000
#define LEDC_LOW_SPEED_MODE 0
#define LEDC_TIMER_3 3
#define LEDC_TIMER_8_BIT 8
#define LEDC_CHANNEL_7 7
#define LEDC_CHANNEL_6 6
#define LEDC_INTR_DISABLE 0
#define TAG "test"
#define pdMS_TO_TICKS(x) (x)
#define ESP_RETURN_ON_ERROR(x,...) do {int err_=(x);if(err_)return err_;} while(0)
typedef struct {bool carrier_present;} lf_rfid_field_report_t;
typedef struct {int gpio_num,speed_mode,channel,intr_type,timer_sel,duty,hpoint;struct {int output_invert;} flags;} ledc_channel_config_t;
static int64_t s_field_started_us;
static int64_t esp_timer_get_time(void){return 1000;}
static int lf_rfid_reader_quiesce(void){return 0;}
static bool s_initialized=true,s_field_on,allowed=true,external,claimed,reserved;
static uint32_t s_requested_hz=125000,s_generated_hz=125000,freq=125000;
static int pins[8],duty[8],pending[8],fail_at,op,active_ms,expected_pin;
static int step(void){return ++op==fail_at?ESP_FAIL:ESP_OK;}
static void check(void){assert(!(duty[7]&&duty[6]));}
static int ledc_channel_config(const ledc_channel_config_t *c){assert(c->speed_mode==0&&c->timer_sel==3&&c->duty==0);pins[c->channel]=c->gpio_num;duty[c->channel]=0;return 0;}
static bool rf_frontend_can_access(int m){assert(m==1);return allowed;}
static int lf_rfid_init(void){return s_initialized?0:ESP_FAIL;}
static int rf_frontend_reserve(int m){assert(m==1);if(claimed||reserved||!allowed)return ESP_ERR_INVALID_STATE;reserved=true;return 0;}
static int rf_frontend_unreserve(int m){assert(m==1&&reserved&&!claimed);reserved=false;return 0;}
static int rf_frontend_claim(int m){assert(m==1&&!claimed);claimed=true;return 0;}
static void rf_frontend_release(int m){assert(m==1);claimed=false;}
static int rf_frontend_get_mode(void){return claimed?1:0;}
static int ledc_set_freq(int m,int t,uint32_t hz){assert(m==0&&t==3);if(step())return ESP_FAIL;freq=hz;return 0;}
static uint32_t ledc_get_freq(int m,int t){assert(m==0&&t==3);return freq;}
static int ledc_set_duty(int m,int c,int d){assert(m==0);if(step())return ESP_FAIL;pending[c]=d;return 0;}
static int ledc_update_duty(int m,int c){assert(m==0);if(step())return ESP_FAIL;duty[c]=pending[c];check();return 0;}
static int ledc_stop(int m,int c,int level){assert(m==0&&level==0);pending[c]=duty[c]=0;return 0;}
static int gpio_set_level(int p,int l){assert((p==2||p==11)&&l==0);return 0;}
static int set_measurement_interrupts(bool c,bool d){assert(!c&&!d);return 0;}
static int measure_feedback(uint32_t ms,uint32_t ref,uint32_t generated,bool active,lf_rfid_field_report_t *r){assert(ms>0&&ref==125000&&!generated&&!active);assert(duty[7]==0&&duty[6]==0);r->carrier_present=external;return 0;}
static void vTaskDelay(int ms){check();if(expected_pin==11){assert(duty[7]==0&&duty[6]>0&&pins[6]==11);}else{assert(expected_pin==2&&duty[7]==128&&duty[6]==0&&pins[7]==2);}active_ms+=ms;}
"""
        main = r"""
int main(void){
 assert(configure()==0);assert(pins[7]==2&&pins[6]==11);
 assert(lf_rfid_field_start(125000,500)==0);
 assert(claimed&&s_field_on&&duty[7]==128&&duty[6]==0&&freq==125000);
 assert(lf_rfid_field_start(126000,500)==ESP_ERR_INVALID_STATE);
 assert(lf_rfid_field_start(125000,500)==0);
 lf_rfid_field_stop();assert(!claimed&&!s_field_on&&duty[7]==0&&duty[6]==0);
 for(int failure=1;failure<=5;failure++){
  op=0;fail_at=failure;
  assert(lf_rfid_field_start(125000,500)!=0);
  assert(!claimed&&!s_field_on&&duty[7]==0&&duty[6]==0);
 }
 fail_at=0;op=0;external=true;
 lf_rfid_field_report_t r;
 assert(lf_rfid_listen_external(100,&r)==0&&r.carrier_present);
 assert(!claimed&&duty[7]==0&&duty[6]==0);
 expected_pin=11;
 assert(lf_rfid_modulation_test(1000,1000,&r)==0&&active_ms==1000);
 assert(!claimed&&duty[7]==0&&duty[6]==0&&freq==125000);
 assert(lf_rfid_field_start(125000,500)==0&&duty[7]==128&&duty[6]==0);
 lf_rfid_field_stop();external=false;active_ms=0;
 assert(lf_rfid_modulation_test(1000,1000,&r)==ESP_ERR_NOT_FOUND&&active_ms==0);
 assert(duty[7]==0&&duty[6]==0&&!claimed);
 assert(lf_rfid_field_start(1000,500)==ESP_ERR_INVALID_ARG);
 uint32_t generated=0;
 expected_pin=2;
 for(uint32_t hz=20000;hz<=200000;hz+=10000){
  active_ms=0;
  assert(lf_rfid_scope_drive(hz,5000,&generated)==0);
  assert(generated==hz&&active_ms==5000&&!claimed&&!reserved&&!s_field_on);
  assert(duty[7]==0&&duty[6]==0);
 }
 assert(lf_rfid_field_start(20000,500)==ESP_ERR_INVALID_ARG);
 assert(lf_rfid_field_start(200000,500)==ESP_ERR_INVALID_ARG);
 assert(lf_rfid_scope_drive(19999,5000,&generated)==ESP_ERR_INVALID_ARG);
 assert(lf_rfid_scope_drive(200001,5000,&generated)==ESP_ERR_INVALID_ARG);
 assert(lf_rfid_scope_drive(20000,5001,&generated)==ESP_ERR_INVALID_ARG);
 assert(lf_rfid_scope_drive(20000,0,&generated)==ESP_ERR_INVALID_ARG);
 assert(lf_rfid_scope_drive(20000,5000,NULL)==ESP_ERR_INVALID_ARG);
 external=true;active_ms=0;
 assert(lf_rfid_scope_drive(20000,5000,&generated)==ESP_ERR_INVALID_STATE);
 assert(active_ms==0&&!reserved&&!claimed&&duty[7]==0&&duty[6]==0);
 external=false;
 assert(lf_rfid_field_start(125000,500)==0);
 assert(lf_rfid_scope_drive(20000,5000,&generated)==ESP_ERR_INVALID_STATE);
 assert(claimed&&s_field_on&&duty[7]==128&&duty[6]==0);
 lf_rfid_field_stop();s_initialized=false;
 assert(lf_rfid_scope_drive(20000,5000,&generated)==ESP_FAIL&&!reserved&&!claimed);
 s_initialized=true;
 allowed=false;assert(lf_rfid_field_start(125000,500)==ESP_ERR_INVALID_STATE);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)
            code='\n'.join('// '+line for line in lab.NOTICE.splitlines())+'\n'+prelude+'\n'+constants+'\nstatic int configure(void){\n'+channel+'\nreturn 0;}\n'+field+load+main
            (path/'test.c').write_text(code)
            subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(path/'test.c'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)


if __name__ == '__main__':
    unittest.main()
