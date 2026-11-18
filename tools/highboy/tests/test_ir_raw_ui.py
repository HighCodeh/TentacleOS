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

from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[3]
source=(root/'firmware_p4/components/Applications/ui/screens/infrared/ir_raw_ui.c').read_text()
start=source.index('static unsigned scope_points(');end=source.index('\nstatic void build_scope_card',start)
code=r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#define SCOPE_SYMBOLS 64
#define SCOPE_HIGH 6
#define SCOPE_LOW 38
typedef struct { int32_t x,y; } lv_point_precise_t;
static lv_point_precise_t s_points[SCOPE_SYMBOLS*4];
static struct { uint16_t duration0,duration1; bool level0,level1; } s_raw[128];
static size_t s_raw_count;
"""+source[start:end]+r"""
int main(void) {
 assert(scope_points(200)==0);s_raw_count=1;assert(scope_points(200)==0);
 s_raw[0].duration0=100;s_raw[0].duration1=300;s_raw[0].level0=true;
 assert(scope_points(200)==4);
 assert(s_points[0].x==0&&s_points[0].y==6&&s_points[1].x==50&&s_points[2].x==50&&s_points[2].y==38&&s_points[3].x==200);
 s_raw_count=128;
 for(unsigned i=0;i<128;i++){s_raw[i].duration0=32767;s_raw[i].duration1=32767;}
 for(int w=0;w<2;w++) {
   int width=w?204:191;
   unsigned n=scope_points(width);assert(n==256);
   assert(s_points[n-1].x==width);
   for(unsigned i=0;i<n;i++) {assert(s_points[i].x>=0&&s_points[i].x<=width);if(i)assert(s_points[i].x>=s_points[i-1].x);}
 }
 puts("PASS: real IR timing projection, empty capture, zero duration, 64-symbol bound, monotonic width and both display widths");
}
"""
with tempfile.TemporaryDirectory(prefix='highboy-raw-ui-') as directory:
 p=Path(directory)/'test.c';p.write_text(code);b=Path(directory)/'test'
 subprocess.run(['cc','-std=c11','-fsanitize=undefined',str(p),'-o',str(b)],check=True)
 subprocess.run([str(b)],check=True)
