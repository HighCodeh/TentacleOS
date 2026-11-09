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

"""Replay cable transitions through the production voltage-SoC filter."""
from pathlib import Path
import re
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[3]
source=(ROOT/"firmware_p4/components/Drivers/bq25896/battery_service.c").read_text()
start=source.index("static int clamp_pct(")
end=source.index("static void battery_task(",start)
macros="\n".join(line for line in source.splitlines() if line.startswith("#define SOC_"))
code=r"""
#include <assert.h>
#include <stdbool.h>
#define CHARGE_STATUS_CHARGE_DONE 3
typedef struct {int soc,chg;bool charging,power_good;} bq25896_telem_t;
"""+macros+"\n"+source[start:end]+r"""
static void replay(battery_service_filter_t *f,bq25896_telem_t *t,int polls,int expected) {
 for(int i=0;i<polls;++i) assert(filter_soc(f,t)==expected);
}
int main(void) {
 battery_service_filter_t f={0};
 bq25896_telem_t t={.soc=78,.power_good=true,.charging=true};
 assert(filter_soc(&f,&t)==78);
 // User regression: unplug, 3.90 V means raw=70, stable for three minutes.
 t.soc=70;t.power_good=false;t.charging=false;
 replay(&f,&t,180,78);
 replay(&f,&t,600,78);
 // An actual sustained voltage drop still moves the estimate, bounded in speed.
 t.soc=60;
 int previous=f.soc;
 for(int i=0;i<600;++i) {
  int current=filter_soc(&f,&t);
  assert(current<=previous && previous-current<=1);previous=current;
 }
 assert(f.soc==67);
 // Repeated source switches must not manufacture extra charge.
 for(int n=0;n<5;++n) {
  t.soc=90;t.power_good=true;t.charging=true;replay(&f,&t,180,67);
  t.soc=60;t.power_good=false;t.charging=false;replay(&f,&t,180,67);
 }
 // Empty endpoint must remain reachable despite the positive compensation.
 t.soc=0;for(int i=0;i<3000;++i) filter_soc(&f,&t);assert(f.soc==0);
 // New readings after failed startup initialize directly, rather than sticking at zero.
 f=(battery_service_filter_t){0};t=(bq25896_telem_t){.soc=60};assert(filter_soc(&f,&t)==60);
 t.soc=80;t.power_good=true;t.charging=true;replay(&f,&t,180,60);
 // Further voltage rise during charging advances from the transition anchor.
 t.soc=90;for(int i=0;i<900;++i) filter_soc(&f,&t);assert(f.soc==80);
 t.soc=100;for(int i=0;i<900;++i) {filter_soc(&f,&t);assert(f.soc<100);}
 t.charging=false;t.chg=CHARGE_STATUS_CHARGE_DONE;
 for(int i=0;i<180;++i) filter_soc(&f,&t);assert(f.soc==100);
 t.soc=85;t.power_good=false;t.chg=0;replay(&f,&t,180,100);
 // USB idle never counts voltage recovery as charging.
 f=(battery_service_filter_t){0};t=(bq25896_telem_t){.soc=50};filter_soc(&f,&t);
 t.power_good=true;t.soc=70;replay(&f,&t,60,50);
 t.soc=80;replay(&f,&t,180,50);
 return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    src=Path(directory)/"test.c";exe=Path(directory)/"test"
    src.write_text(code)
    subprocess.run(["cc","-std=c11","-Wall","-Wextra",str(src),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print("PASS: stable 3.90 V, repeated cable transitions, real voltage changes, endpoints and idle USB")
