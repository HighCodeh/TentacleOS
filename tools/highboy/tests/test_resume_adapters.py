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
import subprocess,sys,tempfile
root=Path(__file__).resolve().parents[3];ui=root/'firmware_p4/components/Applications/ui'
NOTICE=(ui/'screens/home/home_ui.c').read_text().split('#include',1)[0]
def fn(path,name):
 s=(ui/path).read_text();a=s.index(name);z=s.index('{',a)+1;depth=1
 while depth:depth+=(s[z]=='{')-(s[z]=='}');z+=1
 return s[a:z]
common='''#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
static bool resuming,s_saved;
static struct {int sel,count;}s_cr;
static bool ui_tasks_is_resuming(void){return resuming;}
static void capture_result_next(void *unused){(void)unused;s_cr.sel++;}
static void build_screen(void){s_cr.sel=0;s_cr.count=4;}
'''
cases={
'ir':common+'''
enum{LEVEL_PROTOCOLS,LEVEL_FILES,LEVEL_ACTIONS};
static int s_level,s_proto,s_file,s_file_count;
static char s_proto_name[16];
static void reload_all(void){}
static void filter_for_proto(const char *p){(void)p;s_file_count=2;}
'''+fn('screens/infrared/ir_saved_ui.c','void ui_ir_saved_open(void) {')+'''
int main(void){resuming=true;s_level=LEVEL_ACTIONS;s_proto=3;s_file=1;s_cr.sel=2;s_saved=true;
ui_ir_saved_open();assert(s_level==LEVEL_ACTIONS&&s_proto==3&&s_file==1&&s_cr.sel==2&&s_saved);
resuming=false;ui_ir_saved_open();assert(s_level==LEVEL_PROTOCOLS&&s_proto==0&&s_file==0&&!s_saved);
puts("PASS: production IR resume preserves Rename action, protocol, file and saved state; fresh entry resets");}
''',
'rfid':common+'''
enum{VIEW_LIST,VIEW_OPTIONS,VIEW_ADD};
static int s_view,s_saved_sel,s_pending_view,marked;static bool s_card_revealed;
static void store_init(void){} static void stop_scan_timers(void){}
static void capture_result_mark_saved(void *unused){(void)unused;marked++;}
'''+fn('screens/rfid/rfid_menu_ui.c','void ui_rfid_menu_open(void) {')+'''
int main(void){resuming=true;s_view=VIEW_OPTIONS;s_saved_sel=4;s_cr.sel=1;s_saved=true;s_card_revealed=true;
ui_rfid_menu_open();assert(s_view==VIEW_OPTIONS&&s_saved_sel==4&&s_cr.sel==1&&marked==1&&s_saved&&s_card_revealed);
resuming=false;ui_rfid_menu_open();assert(s_view==VIEW_LIST&&s_saved_sel==0&&!s_saved&&!s_card_revealed);
puts("PASS: production RFID resume preserves Save action and card; fresh entry resets");}
'''}
with tempfile.TemporaryDirectory(prefix='highboy-resume-') as temp:
 for name,code in cases.items():
  p=Path(temp)/(name+'.c');p.write_text(NOTICE+code);binary=p.with_suffix('')
  subprocess.run(['cc','-std=c11','-fsanitize=undefined',str(p),'-o',str(binary)],check=True);subprocess.run([str(binary)],check=True)
