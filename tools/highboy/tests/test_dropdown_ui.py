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

"""Exercise quick radio controls with real LVGL and a fake asynchronous service."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "firmware_p4/components/Applications/ui"
SERVICE = ROOT / "firmware_p4/components/Service"
BASE = Path(sys.argv[1]).resolve()
WORK = BASE / "dropdown"
WORK.mkdir(parents=True, exist_ok=True)
SOURCE = UI / "components/dropdown/dropdown_ui.c"
NOTICE = SOURCE.read_text().split("#include", 1)[0]
for name in ("freertos/FreeRTOS.h", "freertos/task.h", "st7789.h", "sys_prio.h",
             "assets_manager.h", "audio_i2s.h", "battery_service.h", "buttons_gpio.h",
             "header_ui.h", "lv_port_indev.h", "msgbox_ui.h", "power_policy.h",
             "reboot_ui.h", "tutorial_ui.h", "ui_manager.h", "ui_metrics.h",
             "ui_theme.h", "vfs_sdcard.h", "led_control.h"):
    path = WORK / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(NOTICE + "#pragma once\n")
(WORK / "esp_err.h").write_text(NOTICE + """
#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
static const char *esp_err_to_name(int error) {(void)error; return "failure";}
""")
(WORK / "esp_log.h").write_text(NOTICE + """
#define ESP_LOGD(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
""")
code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "connection_settings.h"
#include "notify_ui.h"
enum {INPUT_BTN_UP, INPUT_BTN_DOWN, INPUT_BTN_LEFT, INPUT_BTN_RIGHT, INPUT_BTN_OK, INPUT_BTN_BACK};
#define POWER_POLICY_BACK_HOLD_MS 5000
static bool pressed[6], suppressed, modal, sd_present = true;
typedef enum {SCREEN_HOME, SCREEN_MENU, SCREEN_SETTINGS, SCREEN_WIFI_AP_LIST, SCREEN_FILES} screen_id_t;
static screen_id_t current_screen;
static unsigned input_locks;
static unsigned requests, warnings, notices;
static char last_notice[64];
static connection_settings_status_t mock_status;
static tos_config_wifi_t pending_wifi;
static tos_config_ble_t pending_ble;
static int request_error;
tos_config_wifi_t g_config_wifi;
tos_config_ble_t g_config_ble;
tos_config_screen_t g_config_screen = {.brightness=80};
tos_config_system_t g_config_system = {.volume=45};
static struct {lv_color_t screen_base,bg_secondary,border_accent,border_inactive,text_main,border_interface;} current_theme;
static bool ui_nav_pressed(int button) {return pressed[button];}
static bool ok_button_is_down(void) {return pressed[INPUT_BTN_OK];}
static bool back_button_is_down(void) {return pressed[INPUT_BTN_BACK];}
static bool msgbox_is_open(void) {return modal;}
static bool ui_input_is_locked(void) {return false;}
static bool tutorial_is_active(void) {return false;}
static screen_id_t ui_current_screen(void) {return current_screen;}
static void ui_input_lock(int ms) {(void)ms;++input_locks;}
static void lv_port_indev_set_suppressed(bool value) {suppressed=value;}
static int ui_screen_h(void) {return lv_display_get_vertical_resolution(NULL);}
static int ui_screen_w(void) {return lv_display_get_horizontal_resolution(NULL);}
static void audio_i2s_set_volume(uint8_t value) {(void)value;}
static void lcd_apply_brightness(uint8_t value) {(void)value;}
static void lcd_set_brightness(uint8_t value) {(void)value;}
static void reboot_ui_reboot(void) {}
static bool vfs_sdcard_is_mounted(void) {return sd_present;}
static bool header_ui_sd_usage(int *used) {*used=25; return sd_present;}
static void header_ui_sd_eject(void) {sd_present=false;}
static lv_image_dsc_t *assets_get(const char *path) {(void)path; return NULL;}
typedef struct {bool present,charging,vbus_present,is_soc_estimated; int soc,vbat_mv;} battery_snapshot_t;
static bool battery_service_get(battery_snapshot_t *out) {
  *out=(battery_snapshot_t){.present=true,.soc=50,.vbat_mv=3700,.is_soc_estimated=true}; return true;
}
esp_err_t tos_config_save(const char *path, const char *kind) {(void)path;(void)kind;return ESP_OK;}
static void led_signal_warning(void) {++warnings;}
void notify(notify_type_t type, const char *message) {
  (void)type; ++notices; snprintf(last_notice,sizeof(last_notice),"%s",message);
}
void connection_settings_get_status(connection_settings_status_t *out) {*out=mock_status;}
void connection_settings_get_config(tos_config_wifi_t *wifi, tos_config_ble_t *ble) {
  if(wifi) *wifi=g_config_wifi; if(ble) *ble=g_config_ble;
}
esp_err_t connection_settings_request_wifi(const tos_config_wifi_t *config) {
  assert(!mock_status.is_busy); ++requests; pending_wifi=*config;
  if(request_error) return request_error;
  mock_status.is_busy=true; mock_status.has_result=false; mock_status.kind=CONNECTION_SETTINGS_WIFI; return ESP_OK;
}
esp_err_t connection_settings_request_ble(const tos_config_ble_t *config) {
  assert(!mock_status.is_busy); ++requests; pending_ble=*config;
  if(request_error) return request_error;
  mock_status.is_busy=true; mock_status.has_result=false; mock_status.kind=CONNECTION_SETTINGS_BLE; return ESP_OK;
}
""" + f'\n#include "{SOURCE}"\n' + r"""
static void tick(unsigned ms) {lv_tick_inc(ms); slide_btn_timer_cb(slide_btn_timer);}
static void tap(int button) {pressed[button]=true;tick(50);pressed[button]=false;tick(50);}
static void finish(bool applied, int error) {
  if(applied) {
    if(mock_status.kind==CONNECTION_SETTINGS_WIFI) g_config_wifi=pending_wifi;
    else g_config_ble=pending_ble;
  }
  mock_status.is_busy=false; mock_status.has_result=true;mock_status.is_applied=applied;
  mock_status.error=error; ++mock_status.revision;tick(350);
}
static void animation_done(void) {
  lv_anim_delete(slide_panel,NULL);lv_obj_set_y(slide_panel,slide_open?0:-s_panel_h);slide_done_cb(NULL);
}
static void bounds(lv_obj_t *obj) {
  lv_area_t a;lv_obj_get_coords(obj,&a);
  if(a.x1<0||a.x2>=lv_display_get_horizontal_resolution(NULL)||a.y1<0||a.y2>=ui_screen_h())
    fprintf(stderr,"Overflow: %d,%d..%d,%d in %dx%d\n",(int)a.x1,(int)a.y1,(int)a.x2,(int)a.y2,
      (int)lv_display_get_horizontal_resolution(NULL),ui_screen_h());
  assert(a.x1>=0&&a.x2<lv_display_get_horizontal_resolution(NULL)&&a.y1>=0&&a.y2<ui_screen_h());
  for(unsigned i=0;i<lv_obj_get_child_count(obj);++i) bounds(lv_obj_get_child(obj,i));
}
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *buffer) {
  (void)area;(void)buffer;lv_display_flush_ready(display);
}
int main(void) {
  lv_init();lv_display_t *display=lv_display_create(240,320);
  _Alignas(LV_DRAW_BUF_ALIGN) static uint8_t buffer[320*20*2];
  lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display,flush);
  for(unsigned orientation=0;orientation<2;orientation++) for(unsigned light=0;light<2;light++) {
    lv_display_set_resolution(display,orientation?320:240,orientation?240:320);
    current_theme.screen_base=lv_color_hex(light?0xffffff:0x111111);
    current_theme.bg_secondary=lv_color_hex(light?0xdddddd:0x222222);
    current_theme.text_main=lv_color_hex(light?0x111111:0xffffff);
    current_theme.border_accent=lv_color_hex(light?0x005599:0x66bbff);
    current_theme.border_inactive=lv_color_hex(0x888888);
    current_theme.border_interface=current_theme.border_accent;
    g_config_wifi=(tos_config_wifi_t){.ap={.ssid="Keep Wi-Fi",.password="keep-pass",.max_conn=4}};
    g_config_ble=(tos_config_ble_t){.name="Keep Bluetooth",.max_conn=2,.advertising=false};
    mock_status=(connection_settings_status_t){0};requests=0;request_error=0;
    dropdown_ui_create(lv_layer_top());
    for(current_screen=SCREEN_SETTINGS;current_screen<=SCREEN_FILES;current_screen++) {
      unsigned locks_before=input_locks;
      pressed[INPUT_BTN_UP]=true;tick(50);tick(450);tick(1000);
      assert(!slide_open&&!suppressed&&input_locks==locks_before&&pressed[INPUT_BTN_UP]);
      pressed[INPUT_BTN_UP]=false;tick(50);
    }
    current_screen=SCREEN_SETTINGS;pressed[INPUT_BTN_UP]=true;tick(50);tick(450);
    current_screen=SCREEN_HOME;tick(500);assert(!slide_open);
    pressed[INPUT_BTN_UP]=false;tick(50);
    current_screen=orientation?SCREEN_MENU:SCREEN_HOME;
    tap(INPUT_BTN_UP);assert(!slide_open&&!suppressed);
    pressed[INPUT_BTN_UP]=true;tick(50);tick(450);
    assert(slide_open&&suppressed);tick(500);assert(slide_open);
    pressed[INPUT_BTN_UP]=false;tick(50);animation_done();
    lv_obj_update_layout(slide_panel);bounds(slide_panel);
    tap(INPUT_BTN_UP);assert(focus_row==ROW_BADGES);
    tap(INPUT_BTN_OK);assert(requests==1&&mock_status.is_busy&&!s_radio_on[CONNECTION_SETTINGS_WIFI]);
    assert(pending_wifi.enabled&&!strcmp(pending_wifi.ap.password,"keep-pass")&&pending_wifi.ap.max_conn==4);
    tap(INPUT_BTN_RIGHT);tap(INPUT_BTN_OK);assert(requests==1);
    finish(false,ESP_FAIL);assert(!s_radio_on[CONNECTION_SETTINGS_WIFI]&&strstr(last_notice,"failed"));
    tap(INPUT_BTN_LEFT);tap(INPUT_BTN_OK);finish(true,ESP_OK);
    assert(s_radio_on[CONNECTION_SETTINGS_WIFI]&&!strcmp(last_notice,"Wi-Fi on"));
    assert(lv_color_eq(lv_obj_get_style_bg_color(badge_dot[BADGE_WIFI],0),current_theme.border_accent));
    tap(INPUT_BTN_OK);finish(true,ESP_FAIL);
    assert(!s_radio_on[CONNECTION_SETTINGS_WIFI]&&!strcmp(last_notice,"Applied; save failed"));
    tap(INPUT_BTN_RIGHT);tap(INPUT_BTN_OK);
    assert(pending_ble.enabled&&!pending_ble.advertising&&pending_ble.max_conn==2&&!strcmp(pending_ble.name,"Keep Bluetooth"));
    tap(INPUT_BTN_BACK);animation_done();assert(!slide_open&&!suppressed&&mock_status.is_busy);
    finish(true,ESP_OK);assert(g_config_ble.enabled&&!strcmp(last_notice,"Bluetooth on"));
    dropdown_open();animation_done();tap(INPUT_BTN_UP);tap(INPUT_BTN_RIGHT);
    tap(INPUT_BTN_OK);finish(true,ESP_OK);assert(!g_config_ble.enabled);
    request_error=ESP_FAIL;tap(INPUT_BTN_OK);assert(!g_config_ble.enabled&&strstr(last_notice,"unavailable"));
    unsigned previous=requests;modal=true;tap(INPUT_BTN_OK);assert(requests==previous);modal=false;
    tap(INPUT_BTN_BACK);animation_done();
    lv_display_set_resolution(display,orientation?240:320,orientation?320:240);
    current_theme.border_accent=lv_color_hex(0x9955aa);g_config_wifi.enabled=true;
    dropdown_open();animation_done();lv_obj_update_layout(slide_panel);bounds(slide_panel);
    assert(s_radio_on[CONNECTION_SETTINGS_WIFI]);
    assert(lv_color_eq(lv_obj_get_style_bg_color(badge_dot[BADGE_WIFI],0),current_theme.border_accent));
    lv_timer_delete(slide_btn_timer);slide_btn_timer=NULL;lv_obj_delete(slide_panel);slide_panel=NULL;
    suppressed=false;
  }
  puts("PASS: Home/main-menu-only gesture, unblocked submenu holds, quick radio toggles, pending/failure/save results, BACK during apply, themes and both orientations");
}
"""
test = WORK / "test.c"
test.write_text(NOTICE + code)
command = ["cc", "-std=gnu11", "-g", "-fsanitize=undefined,bounds", "-fno-sanitize-recover=all",
           "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_KCONFIG_IGNORE", "-I" + str(WORK), "-I" + str(BASE)]
for folder in (UI / "components/dropdown/include", UI / "components/notify/include",
               SERVICE / "connection_settings/include", SERVICE / "storage_api/include",
               SERVICE / "storage_vfs/include",
               ROOT / "firmware_p4/managed_components/lvgl__lvgl"):
    command += ["-I" + str(folder)]
command += [str(test), str(BASE / "build/lvgl-build/lib/liblvgl.a"), "-lm", "-o", str(WORK / "test")]
subprocess.run(command, check=True, timeout=60)
subprocess.run([str(WORK / "test")], check=True, timeout=30)
