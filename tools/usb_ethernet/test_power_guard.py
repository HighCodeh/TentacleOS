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

"""Exercise the retained power guard without USB hardware."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "firmware_p4/components/Service/ethernet_share/ethernet_share.c").read_text()
start = source.index("      battery_charge_snapshot_t battery = {0};")
end = source.index("      next_power =", start)
fixture = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define ETHERNET_SHARE_POWER_DETAIL_SIZE 128
#define BOOST_MIN_MV 3500
#define ESP_ERR_INVALID_STATE 1
#define portENTER_CRITICAL(x) ((void)0)
#define portEXIT_CRITICAL(x) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef struct {bool adc_valid,boost_fault,thermal_regulation,otg_enabled; unsigned vbat_mv,fault_latched,fault_live;} telem_t;
typedef struct {bool valid; const char *status; telem_t charger;} battery_charge_snapshot_t;
static battery_charge_snapshot_t sample;
static bool available=true;
static struct {char power_detail[128];} s_snapshot;
static bool battery_service_get_charge(battery_charge_snapshot_t *out){*out=sample;return available;}
static int check(void){int ret=0;do {
/* PRODUCTION_POWER_GUARD */
}while(0);return ret;}
int main(void){
sample.valid=true;sample.charger.adc_valid=true;sample.charger.vbat_mv=4000;
assert(check()==0);
sample.charger.vbat_mv=3499;assert(check()==1);assert(strstr(s_snapshot.power_detail,"below"));
sample.charger.vbat_mv=3500;assert(check()==0);
sample.charger.boost_fault=true;sample.charger.fault_latched=64;assert(check()==1);assert(strstr(s_snapshot.power_detail,"Boost fault"));assert(strstr(s_snapshot.power_detail,"40/00"));
sample.charger.boost_fault=false;sample.charger.thermal_regulation=true;assert(check()==1);assert(strstr(s_snapshot.power_detail,"thermal"));
sample.charger.thermal_regulation=false;sample.charger.adc_valid=false;assert(check()==1);assert(strstr(s_snapshot.power_detail,"ADC"));
sample.charger.adc_valid=true;available=false;sample.status="Charger unavailable";assert(check()==1);assert(strstr(s_snapshot.power_detail,"Charger unavailable"));
sample.status=NULL;assert(check()==1);assert(strstr(s_snapshot.power_detail,"snapshot unavailable"));
}
"""
fixture = source.split("#include", 1)[0] + fixture.replace("/* PRODUCTION_POWER_GUARD */", source[start:end])
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "test.c").write_text(fixture)
    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", str(path / "test.c"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("Power guard: voltage boundary, invalid snapshot/ADC, boost, thermal and retained detail passed")
