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

"""Exercise production formatting input/callbacks without a physical display."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
source=(ROOT/'firmware_p4/components/Applications/ui/screens/settings/storage_settings_ui.c').read_text()
def function(signature):
    start=source.index(signature); opening=source.index('{',start); end=opening+1; depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}'); end+=1
    return source[start:end]
code=r"""
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
typedef struct { int action, button; } input_event_t;
typedef int lv_event_t;
enum { INPUT_ACTION_PRESS, INPUT_ACTION_REPEAT, INPUT_ACTION_RELEASE };
enum { INPUT_BTN_OK, INPUT_BTN_BACK, INPUT_BTN_UP, INPUT_BTN_DOWN };
#define SCREEN_SETTINGS 1
#define ACT_COUNT 4
#define UI_FB_NAV 1
static bool s_formatting;
static atomic_bool s_format_done, s_format_cancel;
static atomic_uint s_format_percent;
static _Atomic(const char *) s_format_stage;
static void *s_fmt_cancel_label;
static int s_sel, screens, actions;
static void lv_label_set_text(void *label,const char *text) { assert(label); assert(!strcmp(text,"Cancelling...")); }
static void ui_switch_screen(int screen) { (void)screen; screens++; }
static void update_selection(void) {}
static void ui_feedback(int kind) { (void)kind; }
static void fire_action(int index) { (void)index; actions++; }
"""
for name in ['static void cancel_format(', 'static void cancel_format_event(',
             'static bool format_progress(', 'static void storage_settings_input(']:
    code+=function(name)+'\n'
code+=r"""
int main(void) {
 s_formatting=true; s_fmt_cancel_label=&s_sel;
 input_event_t ev={INPUT_ACTION_REPEAT,INPUT_BTN_OK};
 storage_settings_input(&ev,NULL); assert(!s_format_cancel && !actions && !screens);
 ev.action=INPUT_ACTION_PRESS; ev.button=INPUT_BTN_DOWN;
 storage_settings_input(&ev,NULL); assert(!s_format_cancel && !actions);
 ev.button=INPUT_BTN_BACK; storage_settings_input(&ev,NULL);
 assert(s_format_cancel && !screens);
 assert(!format_progress("Allocation table",50,NULL));
 assert(s_format_percent==50 && !strcmp(s_format_stage,"Allocation table"));
 assert(!format_progress(NULL,0,NULL) && s_format_percent==50);
 s_format_cancel=false; ev.button=INPUT_BTN_OK; storage_settings_input(&ev,NULL);
 assert(s_format_cancel && !actions);
 s_format_cancel=false; cancel_format_event(NULL); assert(s_format_cancel);
 s_format_cancel=false; s_format_done=true; cancel_format_event(NULL); assert(!s_format_cancel);
 s_fmt_cancel_label=NULL; s_format_done=false; cancel_format_event(NULL); assert(s_format_cancel);
 s_formatting=false; ev.button=INPUT_BTN_BACK; storage_settings_input(&ev,NULL); assert(screens==1);
}
"""
with tempfile.TemporaryDirectory(prefix='hb-format-ui-') as temporary:
    work=Path(temporary); (work/'test.c').write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
print('PASS: format UI cancellation via OK/BACK/touch, no repeated actions, detached overlay')
