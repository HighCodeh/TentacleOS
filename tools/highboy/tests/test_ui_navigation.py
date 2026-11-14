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

"""Run the production UI input router against queued gesture regressions."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "firmware_p4/components/Applications/ui"
source = (UI / "ui_manager.c").read_text()
NOTICE = source.split("#include", 1)[0]

def function(signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

header = (ROOT / "firmware_p4/components/Drivers/input_manager/include/input_manager.h").read_text()
types = header[header.index("typedef enum"):header.index("/**", header.index("} input_event_t;"))]
fakes = r"""
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
typedef void lv_timer_t;
typedef void (*ui_input_handler_t)(const input_event_t *, void *);
static ui_input_handler_t s_screen_handler;
static void *s_screen_handler_ctx;
static uint32_t s_consumed_buttons, s_back_pressed_at;
static bool s_back_pending;
static bool held[INPUT_BTN_COUNT], locked, modal, dropdown, keyboard, tips, tutorial;
static int screen, events, legacy, cancelled, waits;
static void *indev_keypad = (void *)1, *main_group = (void *)1;
static input_event_t queue[32];
static unsigned head, tail;
#define POWER_POLICY_BACK_HOLD_MS 5000
#define LV_KEY_ESC 27
static bool input_get_event(input_event_t *ev, int timeout) {
 (void)timeout; if(head==tail) return false; *ev=queue[head++]; return true;
}
static bool input_is_down(input_button_t b) { return held[b]; }
static void lv_indev_wait_release(void *p) { assert(p); waits++; }
static uint32_t input_lock_until = 1;
static uint32_t lv_tick_get(void) { return locked ? 0 : 1; }
static bool ui_input_is_locked(void);
static bool msgbox_is_open(void) { return modal; }
static bool dropdown_ui_is_open(void) { return dropdown; }
static bool keyboard_is_open(void) { return keyboard; }
static bool screen_tips_active(void) { return tips; }
static bool tutorial_is_active(void) { return tutorial; }
static bool power_policy_is_asleep(void) { return false; }
static bool lvgl_glue_is_landscape(void) { return false; }
static void ui_input_consume_gesture(void);
static void keyboard_close(void) { cancelled++; keyboard=false; ui_input_consume_gesture(); }
static void screen_tips_handle_input(const input_event_t *ev) {
 if(ev->action==INPUT_ACTION_PRESS) tips=false;
}
static void lv_group_send_data(void *g, int key) { assert(g && key==LV_KEY_ESC); legacy++; }
"""
# The implementation is compiled directly, not reimplemented by the harness.
functions = "\n".join(function(signature) for signature in (
    "void ui_input_consume_gesture(void)",
    "void ui_input_set_screen_handler(",
    "bool ui_input_is_locked(void)",
    "static bool input_dispatch_blocked(void)",
    "static input_button_t input_remap_for_rotation(",
    "static bool keypad_dispatch_blocked(void)",
    "static void ui_input_pump("))
functions = functions.replace("void ui_input_consume_gesture(void)", "static void ui_input_consume_gesture(void)", 1)
tests = r"""
static void handler(const input_event_t *ev, void *ctx) {
 (void)ctx; events++;
 if(ev->button==INPUT_BTN_BACK) {
   assert(ev->action==INPUT_ACTION_PRESS);
   screen--;
 }
}
static void push(input_button_t b, input_action_t a, uint32_t time) {
 assert(tail<32); queue[tail++]=(input_event_t){b,a,time};
 held[b]=a!=INPUT_ACTION_RELEASE;
}
static void send(input_button_t b, input_action_t a, uint32_t time) {
 push(b,a,time); ui_input_pump(NULL); head=tail=0;
}
static void back(uint32_t time) {
 send(INPUT_BTN_BACK,INPUT_ACTION_PRESS,time);
 send(INPUT_BTN_BACK,INPUT_ACTION_RELEASE,time+100);
}
int main(void) {
 screen=3; ui_input_set_screen_handler(handler,NULL);
 assert(keypad_dispatch_blocked());
 send(INPUT_BTN_BACK,INPUT_ACTION_PRESS,100);
 send(INPUT_BTN_BACK,INPUT_ACTION_REPEAT,600);
 send(INPUT_BTN_BACK,INPUT_ACTION_LONG_PRESS,900);
 assert(events==0 && screen==3 && legacy==0);
 send(INPUT_BTN_BACK,INPUT_ACTION_RELEASE,1000);
 assert(events==1 && screen==2 && legacy==0);
 back(1200); assert(screen==1 && events==2);
 // Holding for shutdown never navigates, even before the policy timer fires.
 send(INPUT_BTN_BACK,INPUT_ACTION_PRESS,2000);
 send(INPUT_BTN_BACK,INPUT_ACTION_RELEASE,7000);
 assert(events==2);
 // Opening/closing a popup consumes the entire initiating and closing gestures.
 send(INPUT_BTN_BACK,INPUT_ACTION_PRESS,8000);
 modal=true; ui_input_consume_gesture();
 send(INPUT_BTN_BACK,INPUT_ACTION_RELEASE,14000);
 send(INPUT_BTN_OK,INPUT_ACTION_PRESS,15000);
 modal=false; ui_input_consume_gesture();
 send(INPUT_BTN_OK,INPUT_ACTION_RELEASE,15100);
 assert(events==2);
 back(16000); assert(events==3);
 // A transition drops queued input and ignores repeats until release.
 push(INPUT_BTN_OK,INPUT_ACTION_PRESS,17000);
 ui_input_set_screen_handler(handler,NULL);
 send(INPUT_BTN_OK,INPUT_ACTION_REPEAT,17500);
 send(INPUT_BTN_OK,INPUT_ACTION_RELEASE,17600);
 assert(events==3);
 send(INPUT_BTN_OK,INPUT_ACTION_PRESS,18000);
 send(INPUT_BTN_OK,INPUT_ACTION_RELEASE,18100);
 assert(events==5);
 // Keyboard BACK closes only the keyboard; the next BACK belongs to the screen.
 keyboard=true; assert(!keypad_dispatch_blocked());
 back(19000); assert(cancelled==1 && events==5);
 back(20000); assert(events==6);
 // The legacy menu receives exactly one ESC, on release, never the held repeat.
 ui_input_set_screen_handler(NULL,NULL);
 assert(!keypad_dispatch_blocked());
 send(INPUT_BTN_BACK,INPUT_ACTION_PRESS,21000);
 send(INPUT_BTN_BACK,INPUT_ACTION_REPEAT,21500); assert(legacy==0);
 send(INPUT_BTN_BACK,INPUT_ACTION_RELEASE,21600); assert(legacy==1);
 modal=true; assert(keypad_dispatch_blocked()); back(22000); assert(legacy==1);
 modal=false; ui_input_consume_gesture(); back(23000); assert(legacy==2);
 tips=true; back(24000); assert(!tips && legacy==2);
 assert(waits>0);
 puts("UI gesture ownership regressions passed");
}
"""
with tempfile.TemporaryDirectory(prefix="highboy-navigation-") as directory:
    test = Path(directory) / "navigation.c"
    test.write_text(NOTICE + "#include <stdint.h>\n" + types + fakes + functions + tests)
    binary = Path(directory) / "navigation"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(test), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)

# Exercise the other delivery path: suppression must cancel LVGL's pending click.
source = (ROOT / "firmware_p4/components/Service/lvgl/lv_port_indev.c").read_text()
keypad_fakes = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
typedef void lv_indev_t;
typedef struct {uint32_t key; int state;} lv_indev_data_t;
enum {LV_KEY_LEFT=1, LV_KEY_RIGHT, LV_KEY_UP, LV_KEY_DOWN, LV_KEY_ENTER, LV_KEY_ESC};
enum {LV_INDEV_STATE_RELEASED, LV_INDEV_STATE_PRESSED};
static bool s_suppressed, blocked, back, ok;
static int waits, s_remote_phase;
static uint32_t s_remote_key;
static bool guard(void) { return blocked; }
static bool (*s_guard)(void) = guard;
static void lv_indev_wait_release(lv_indev_t *p) { (void)p; waits++; }
static bool lvgl_glue_is_landscape(void) { return false; }
static bool up_button_is_down(void) { return false; }
static bool down_button_is_down(void) { return false; }
static bool left_button_is_down(void) { return false; }
static bool right_button_is_down(void) { return false; }
static bool ok_button_is_down(void) { return ok; }
static bool back_button_is_down(void) { return back; }
"""
keypad_tests = r"""
int main(void) {
 lv_indev_data_t data={0};
 back=true; keypad_read(NULL,&data); assert(data.state==LV_INDEV_STATE_RELEASED);
 back=false; ok=true; keypad_read(NULL,&data);
 assert(data.state==LV_INDEV_STATE_PRESSED && data.key==LV_KEY_ENTER);
 blocked=true; keypad_read(NULL,&data);
 assert(data.state==LV_INDEV_STATE_RELEASED && waits==1);
 blocked=false; ok=false; keypad_read(NULL,&data);
 assert(data.state==LV_INDEV_STATE_RELEASED);
 s_suppressed=true; ok=true; keypad_read(NULL,&data);
 assert(data.state==LV_INDEV_STATE_RELEASED && waits==2);
}
"""
with tempfile.TemporaryDirectory(prefix="highboy-keypad-") as directory:
    test = Path(directory) / "keypad.c"
    test.write_text(NOTICE + keypad_fakes + function("static void keypad_read(") + keypad_tests)
    binary = Path(directory) / "keypad"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(test), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("LVGL keypad exclusivity regressions passed")

# Actual screen reconstruction code must preserve the parent subview.
source = (UI / "screens/settings/settings_ui.c").read_text()
settings_open = function("void ui_settings_open(void)")
source = (UI / "screens/lora/lora_chat_ui.c").read_text()
lora_open = function("void ui_lora_chat_open(void)")
source = (UI / "ui_manager.c").read_text()
slot_clear = function("static void screen_slot_clear_cb(")
route_fakes = r"""
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
typedef int screen_id_t;
typedef int settings_view_t;
typedef int lora_proto_t;
typedef int lv_obj_t;
typedef struct {void *slot; lv_obj_t *target;} lv_event_t;
enum {SCREEN_MENU,SCREEN_SETTINGS,SCREEN_SETTINGS_DEV,SCREEN_C5_STATUS,
 SCREEN_HAPTIC,SCREEN_SPEAKER,SCREEN_MIC_REC,SCREEN_SPECTRUM,SCREEN_IMU_MONITOR,
 SCREEN_LORA_CHANNELS,SCREEN_LORA_POSITION,SCREEN_LORA_TELEMETRY,SCREEN_LORA_SECURE_DM,
 SCREEN_LORA_TRACEROUTE,SCREEN_LORA_MQTT,SCREEN_LORA_RNODE};
enum {VIEW_MAIN,VIEW_LAB,VIEW_DEV,VIEW_PROTO,VIEW_HOME};
enum {PROTOCOL_LORA,LORA_PROTO_NONE};
static struct {int target;} LAB_ITEMS[]={{SCREEN_HAPTIC},{SCREEN_SPEAKER},{SCREEN_MIC_REC},
 {SCREEN_SPECTRUM},{SCREEN_IMU_MONITOR}};
#define LAB_COUNT 5
#define ESP_LOGI(...) ((void)0)
static lv_obj_t *s_screen;
static int s_view, s_proto, s_home_sel, s_node, s_rnode_count, s_chat_seq, from, active;
static bool s_nodes_list, s_linked;
static void *s_connect_timer, *s_chat_poll;
static screen_id_t ui_current_screen(void) {return from;}
static void lv_obj_del(lv_obj_t *obj) {(void)obj;}
static void build_settings_view(settings_view_t view) {s_view=view;}
static void ui_theme_set_protocol(int proto) {(void)proto;}
static lora_proto_t lora_session_active(void) {return active;}
static int sel_for_proto(int proto) {return proto;}
static void build_screen(void) {}
static void *lv_event_get_user_data(lv_event_t *event) {return event->slot;}
static lv_obj_t *lv_event_get_target(lv_event_t *event) {return event->target;}
"""
route_tests = r"""
int main(void) {
 from=SCREEN_MENU; s_view=VIEW_LAB; ui_settings_open(); assert(s_view==VIEW_MAIN);
 for(int i=0;i<LAB_COUNT;i++) {
   from=LAB_ITEMS[i].target; ui_settings_open(); assert(s_view==VIEW_LAB);
 }
 from=SCREEN_C5_STATUS; s_view=VIEW_DEV; ui_settings_open(); assert(s_view==VIEW_DEV);
 s_view=VIEW_MAIN; ui_settings_open(); assert(s_view==VIEW_MAIN);
 from=SCREEN_MENU; ui_lora_chat_open(); assert(s_view==VIEW_PROTO);
 from=SCREEN_LORA_CHANNELS; s_proto=7; s_home_sel=3;
 ui_lora_chat_open(); assert(s_view==VIEW_HOME && s_proto==7 && s_home_sel==3);
 from=SCREEN_LORA_POSITION; ui_lora_chat_open(); assert(s_view==VIEW_HOME);
 lv_obj_t old_screen=0, new_screen=1;
 lv_obj_t *slot=&new_screen; lv_event_t event={&slot,&old_screen};
 screen_slot_clear_cb(&event); assert(slot==&new_screen);
 event.target=&new_screen; screen_slot_clear_cb(&event); assert(slot==NULL);
}
"""
with tempfile.TemporaryDirectory(prefix="highboy-routes-") as directory:
    test = Path(directory) / "routes.c"
    test.write_text(NOTICE + route_fakes + settings_open + lora_open + slot_clear + route_tests)
    binary = Path(directory) / "routes"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", str(test), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Settings/LoRa return and stale screen deletion regressions passed")
