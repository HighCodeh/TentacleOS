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

"""Compile the real LF self-test with deterministic measurement failures."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import lab

class SelfTest(unittest.TestCase):
    def test_carrier_is_required_for_frequency_pass(self):
        source = (lab.ROOT/'firmware_p4/components/Drivers/lf_rfid/lf_rfid.c').read_text()
        function = source[source.index('esp_err_t lf_rfid_self_test('):]
        prelude = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_RESPONSE 3
#define RF_FRONTEND_LF_125KHZ 1
#define LF_EXTERNAL_CHECK_MS 30
#define LF_RFID_NOMINAL_HZ 125000
#define LF_RFID_DEFAULT_DUTY_PERMILLE 500
#define LF_SETTLE_MS 5
#define GPIO_RFID_LF_COIL_PIN 2
#define TAG "test"
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGW(...) ((void)0)
#define ESP_RETURN_ON_ERROR(expr, ...) do {int e=(expr);if(e)return e;}while(0)
typedef struct {bool carrier_present;int32_t frequency_error_ppm;} lf_rfid_field_report_t;
typedef struct {bool gpio_static_ok,carrier_present,frequency_ok;lf_rfid_field_report_t field;} lf_rfid_self_test_t;
static bool carrier,external,on;
static int32_t ppm;
static bool rf_frontend_can_access(int x){(void)x;return true;}
static int lf_rfid_init(void){return 0;}
static void lf_rfid_field_stop(void){on=false;}
static int gpio_get_level(int x){(void)x;return 0;}
static int lf_rfid_listen_external(int t,lf_rfid_field_report_t *r){(void)t;r->carrier_present=external;return 0;}
static int lf_rfid_field_start(int h,int d){(void)h;(void)d;on=true;return 0;}
static void vTaskDelay(int t){(void)t;}
static int lf_rfid_measure(int t,lf_rfid_field_report_t *r){(void)t;r->carrier_present=carrier;r->frequency_error_ppm=ppm;return 0;}
"""
        main = r"""
int main(void){
 lf_rfid_self_test_t r;
 carrier=false;ppm=0;
 assert(lf_rfid_self_test(&r)==ESP_ERR_INVALID_RESPONSE);
 assert(!r.carrier_present&&!r.frequency_ok&&!on);
 carrier=true;ppm=0;
 assert(lf_rfid_self_test(&r)==ESP_OK&&r.frequency_ok&&!on);
 ppm=50000;assert(lf_rfid_self_test(&r)==ESP_OK&&r.frequency_ok&&!on);
 ppm=-50000;assert(lf_rfid_self_test(&r)==ESP_OK&&r.frequency_ok&&!on);
 ppm=50001;assert(lf_rfid_self_test(&r)==ESP_ERR_INVALID_RESPONSE&&!r.frequency_ok&&!on);
 external=true;assert(lf_rfid_self_test(&r)==ESP_ERR_INVALID_STATE&&!on);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp);(path/'test.c').write_text('\n'.join('// '+l for l in lab.NOTICE.splitlines())+'\n'+prelude+function+main)
            subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(path/'test.c'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)

if __name__=='__main__':unittest.main()
