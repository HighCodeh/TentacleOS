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

"""Exercise the production charger repair with register and telemetry fault injection."""
from pathlib import Path
import re
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / 'firmware_p4/components/Drivers/bq25896/bq25896.c').read_text()
def function(name):
    start = source.index(name)
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
functions = function('static esp_err_t pack_write_unlocked(') + '\n' + function('esp_err_t bq25896_restore_confirmed_604858(')
constants = '\n'.join(line for line in source.splitlines() if line.startswith('#define ') and '\\' not in line)
registers = source[source.index('enum {'):source.index('};', source.index('enum {'))+2]
prefix = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#define CONFIG_HIGHBOY_BOARD_V203 1
#define ESP_OK 0
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_FAIL -1
#define POWER_PROFILE_CUTOFF_MV 3000
#define ESP_LOGE(...) ((void)0)
typedef int esp_err_t;
typedef struct {
 bool adc_valid, power_good, otg_enabled, is_recovery_required, charge_enabled;
 unsigned fault_live, part_number, vbat_mv, ts_milli_pct;
 unsigned charge_current_limit_ma, charge_voltage_limit_mv;
} bq25896_telem_t;
static bq25896_telem_t sample;
static uint8_t regs[0x15];
static int writes, fail_write, drop_write, samples;
static bool locked, fail_post;
static void *s_dev;
static esp_err_t lock_driver(void) { assert(!locked); locked=true; return 0; }
static void unlock_driver(void) { assert(locked); locked=false; }
static void note_i2c_result(int result) { (void)result; }
static esp_err_t read_reg_unlocked(uint8_t reg, uint8_t *value) {
 assert(locked); *value=regs[reg]; return 0;
}
static esp_err_t i2c_master_transmit(void *dev, const uint8_t *data, size_t size, int timeout) {
 (void)dev; (void)timeout; assert(locked && size==2);
 assert(data[0]!=0x14);
 assert(data[0]!=3 || !(data[1]&0x30));
 if(data[0]!=3) assert(!(regs[3]&0x10));
 ++writes;
 if(writes==fail_write) return ESP_FAIL;
 if(writes!=drop_write) regs[data[0]]=data[1];
 return ESP_OK;
}
static esp_err_t bq25896_read_telemetry(bq25896_telem_t *out) {
 *out=sample;
 if(++samples>1) {
   if(fail_post) return ESP_FAIL;
   out->charge_current_limit_ma=(regs[4]&127)*64;
   out->charge_voltage_limit_mv=3840+(regs[6]>>2)*16;
   out->charge_enabled=(regs[3]&0x10)!=0;
 }
 return 0;
}
"""
suffix = r"""
static void reset(void) {
 memset(regs,0,sizeof(regs));
 sample=(bq25896_telem_t){.adc_valid=true,.power_good=true,.vbat_mv=3800,.ts_milli_pct=54000};
 writes=fail_write=drop_write=samples=0; locked=fail_post=false;
}
int main(void) {
 reset(); regs[3]=0x10;
 assert(bq25896_restore_confirmed_604858()==0);
 assert(regs[3]==0x0a && regs[4]==6 && regs[5]==0x10 && regs[6]==0x56);
 assert(regs[7]==0x8d && regs[0x0d]==0x93 && !locked);
 int total=writes;
 for(int i=1;i<=total;i++) {
   reset();regs[3]=0x10;fail_write=i;
   assert(bq25896_restore_confirmed_604858()!=0 && !(regs[3]&0x10) && !locked);
   reset();regs[3]=0x10;drop_write=i;
   assert(bq25896_restore_confirmed_604858()!=0 && !(regs[3]&0x10) && !locked);
 }
 reset(); sample.vbat_mv=3000; assert(bq25896_restore_confirmed_604858()!=0 && writes==0);
 reset(); sample.vbat_mv=4201; assert(bq25896_restore_confirmed_604858()!=0 && writes==0);
 reset(); sample.is_recovery_required=true; assert(bq25896_restore_confirmed_604858()!=0 && writes==0);
 reset(); sample.ts_milli_pct=49000; assert(bq25896_restore_confirmed_604858()!=0 && writes==0);
 reset(); sample.power_good=false; assert(bq25896_restore_confirmed_604858()!=0 && writes==0);
 reset(); sample.fault_live=1; assert(bq25896_restore_confirmed_604858()!=0 && writes==0);
 reset(); sample.part_number=1; assert(bq25896_restore_confirmed_604858()!=0 && writes==0);
 reset(); regs[3]=0x20; assert(bq25896_restore_confirmed_604858()!=0);
 reset(); fail_post=true; assert(bq25896_restore_confirmed_604858()!=0 && !(regs[3]&0x10));
 puts("PASS: production charge repair, disabled-charge ordering, exact limits, write/readback failures and safety guards");
}
"""
with tempfile.TemporaryDirectory(prefix='highboy-charge-repair-') as tmp:
    c = Path(tmp) / 'test.c'
    c.write_text(prefix + '\n' + registers + '\n' + constants + '\n' + functions + '\n' + suffix)
    exe = Path(tmp) / 'test'
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
