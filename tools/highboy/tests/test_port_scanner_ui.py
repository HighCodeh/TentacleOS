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
#

"""Exercise port-scanner configuration, results and lifecycle with real LVGL."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
UI = ROOT / "firmware_p4/components/Applications/ui"
SERVICE = ROOT / "firmware_p4/components/Service"
BASE = Path(sys.argv[1]).resolve()
WORK = BASE / "port_scanner"
WORK.mkdir(parents=True, exist_ok=True)
NOTICE = (UI / "screens/wifi/wifi_port_scan_ui.c").read_text().split("#include", 1)[0]


def write(name, text):
    path = WORK / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(NOTICE + "#pragma once\n" + text)
    return path


for name in ("st7789.h", "ui_chrome.h", "ui_metrics.h", "ui_feedback.h", "ui_manager.h",
             "ui_theme.h", "assets_manager.h", "led_control.h", "notify_ui.h", "msgbox_ui.h"):
    write(name, "")
write("esp_err.h", """typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_FAIL -1
static const char *esp_err_to_name(int error) { (void)error; return "failure"; }
""")
write("freertos/FreeRTOS.h", """typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
""")
write("esp_log.h", "#define ESP_LOGW(tag, ...) ((void)(tag))\n#define ESP_LOGI(tag, ...) ((void)(tag))\n#define ESP_LOGE(tag, ...) ((void)(tag))\n")
write("esp_attr.h", "#define EXT_RAM_BSS_ATTR\n")
code = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#define SCREEN_WIFI_MENU 1
#define UI_FB_NAV 1
#define NOTIFY_WARNING 1
enum { INPUT_ACTION_PRESS, INPUT_ACTION_REPEAT, INPUT_ACTION_RELEASE };
enum { INPUT_BTN_BACK, INPUT_BTN_UP, INPUT_BTN_DOWN, INPUT_BTN_LEFT, INPUT_BTN_RIGHT, INPUT_BTN_OK };
typedef struct { int action, button; } input_event_t;
typedef struct {
  lv_color_t screen_base, bg_primary, bg_secondary, border_accent, border_interface;
  lv_color_t border_inactive, text_main, text_secondary, bg_item_top, bg_item_bot;
} ui_theme_t;
static ui_theme_t current_theme;
static unsigned backs, notices;
static lv_image_dsc_t *assets_get(const char *path) { (void)path; return NULL; }
static int ui_screen_w(void) { return lv_display_get_horizontal_resolution(NULL); }
static int ui_screen_h(void) { return lv_display_get_vertical_resolution(NULL); }
static void ui_feedback(int event) { assert(event == UI_FB_NAV); }
static void ui_chrome_gradient(lv_obj_t *obj, lv_color_t a, lv_color_t b, int h) {
  (void)b; (void)h; lv_obj_set_style_bg_color(obj, a, 0);
}
static lv_obj_t *ui_chrome_light_title(lv_obj_t *root, const char *title) {
  lv_obj_t *label = lv_label_create(root); lv_label_set_text(label, title);
  lv_obj_set_style_text_color(label,current_theme.text_main,0);
  lv_obj_set_style_text_font(label,&lv_font_montserrat_12,0);
  lv_obj_set_width(label,ui_screen_w()-24); lv_label_set_long_mode(label,LV_LABEL_LONG_DOT);
  lv_obj_align(label,LV_ALIGN_BOTTOM_LEFT,12,-1); return label;
}
#define UI_CHROME_HEADER_H 42
#define UI_CHROME_FOOTER_H 22
static lv_obj_t *ui_chrome_header(lv_obj_t *root,const char *text,const char *icon) {
  (void)icon; lv_obj_t *header=lv_obj_create(root); lv_obj_remove_style_all(header);
  lv_obj_set_size(header,ui_screen_w(),42);
  lv_obj_t *label=lv_label_create(header); lv_label_set_text(label,text);
  lv_obj_set_style_text_color(label,current_theme.border_accent,0);
  lv_obj_set_style_text_font(label,&lv_font_montserrat_12,0); lv_obj_align(label,LV_ALIGN_BOTTOM_LEFT,12,-1);
  return header;
}
static void ui_chrome_footer_set_text(lv_obj_t *footer,const char *text) {
  lv_label_set_text(lv_obj_get_child(footer,0),text);
}
static lv_obj_t *ui_chrome_footer(lv_obj_t *root, const char *text) {
  lv_obj_t *footer = lv_obj_create(root);
  lv_obj_set_size(footer, lv_pct(100), 22);
  lv_obj_set_style_pad_all(footer, 0, 0); lv_obj_set_style_border_width(footer, 0, 0);
  lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(footer,current_theme.bg_primary,0);
  lv_obj_t *label = lv_label_create(footer); lv_obj_set_style_text_color(label,current_theme.text_main,0); lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_center(label); return footer;
}
static void ui_chrome_set_breadcrumb_root(const char *title) { (void)title; }
static void ui_input_set_screen_handler(void (*cb)(const input_event_t *, void *), void *ctx) {
  assert(cb != NULL && ctx == NULL);
}
static void clear_slot(lv_event_t *event) {
  lv_obj_t **slot = lv_event_get_user_data(event);
  if (*slot == lv_event_get_target(event)) *slot = NULL;
}
static void ui_screen_load_owned(lv_obj_t **slot, lv_obj_t *root) {
  lv_obj_t *old=*slot;
  *slot = root; lv_obj_add_event_cb(root, clear_slot, LV_EVENT_DELETE, slot); lv_screen_load(root); if(old) lv_obj_delete(old);
}
static void ui_switch_screen(int screen) { assert(screen == SCREEN_WIFI_MENU); ++backs; }
static void led_signal_warning(void) { ++notices; }
static void led_signal_info(void) { ++notices; }
static void led_signal_error(void) { ++notices; }
static void notify(int type, const char *message) { (void)type; (void)message; ++notices; }
"""

code += r"""
#include "port_scanner.h"
#include "keyboard_ui.h"
static port_scanner_snapshot_t mock_status;
static port_scanner_config_t last_request;
static unsigned requests, stop_requests, details, page_requests, page_closes;
static port_scanner_page_t mock_page;
static port_scanner_browse_request_t requested_browse;
static void (*confirm_cb)(bool);
static unsigned delete_requests;
static char requested_path[PORT_SCANNER_PATH_LENGTH];
static bool keyboard_visible, root_fails, timer_fails, long_banner, udp_results;
static unsigned udp_state;
static keyboard_config_t editor;
static keyboard_submit_cb_t submit_cb;
static void *submit_context;
void keyboard_open_config(const keyboard_config_t *config, keyboard_submit_cb_t cb, void *context) {
  editor=*config; submit_cb=cb; submit_context=context; keyboard_visible=true;
}
bool keyboard_is_open(void) { return keyboard_visible; }
void keyboard_close(void) { keyboard_visible=false; }
void port_scanner_get_snapshot(port_scanner_snapshot_t *out) { *out=mock_status; }
esp_err_t port_scanner_validate(const port_scanner_config_t *config) {
  return strstr(config->target,"invalid") ? ESP_ERR_INVALID_ARG : ESP_OK;
}
esp_err_t port_scanner_start(const port_scanner_config_t *config) {
  assert(!mock_status.is_busy); last_request=*config; ++requests;
  mock_status.is_ping_enabled=config->is_ping_enabled;
  mock_status.is_busy=true; mock_status.state=PORT_SCANNER_RUNNING; return ESP_OK;
}
void port_scanner_stop(void) {
  ++stop_requests; if(mock_status.is_busy) mock_status.state=PORT_SCANNER_STOPPING;
}
bool port_scanner_get_result(uint16_t index, port_scan_result_t *out) {
  if(index>=mock_status.stored) return false;
  *out=(port_scan_result_t){.ip_str="192.168.255.254",.port=65535,.banner="SSH-2.0-example"};
  if(udp_results) {
    out->protocol=PORT_SCAN_PROTO_UDP; out->status=udp_state; out->banner[0]='\0';
  }
  if(long_banner) snprintf(out->banner,sizeof(out->banner),"SSH-2.0-example\nSecond line with more data that exceeds a row");
  return true;
}
esp_err_t port_scanner_browse(const port_scanner_browse_request_t *request) {
  if(mock_status.is_busy || mock_page.is_busy) return ESP_ERR_INVALID_STATE;
  ++page_requests; requested_browse=*request;
  if(request->kind==PORT_SCANNER_BROWSE_DELETE) ++delete_requests;
  snprintf(requested_path,sizeof(requested_path),"%s",request->path);
  mock_page=(port_scanner_page_t){.is_busy=true,.number=request->number}; return ESP_OK;
}
void port_scanner_get_page(port_scanner_page_t *out) { *out=mock_page; }
void port_scanner_close_page(void) { ++page_closes; }
static void msgbox_open(const char *icon, const char *text, const char *button,
                        const char *cancel, void (*cb)(bool)) {
  if(udp_results) {
    assert(strstr(text,"/UDP"));
    assert(strstr(text,udp_state==PORT_SCAN_STATUS_OPEN_FILTERED ? "OPEN|FILTERED" : "FILTERED"));
  } else assert(strstr(text,"SSH-2.0-example") && strstr(text,"/TCP"));
  ++details;
}
static void msgbox_open_ex(const char *icon, const char *text, const char *button,
                           const char *cancel, void (*cb)(bool), bool default_confirm) {
  assert(!default_confirm && cb && strstr(text,"Delete")); confirm_cb=cb;
}
static void msgbox_close(void) { confirm_cb=NULL; }
static lv_obj_t *ui_chrome_screen_create(void) {
  if(root_fails) return NULL;
  lv_obj_t *root=lv_obj_create(NULL); lv_obj_set_style_bg_color(root,current_theme.screen_base,0);
  lv_obj_set_style_pad_all(root,0,0); return root;
}
"""
for path in (UI / "components/toggle/toggle_ui.c",
             UI / "components/intensity_bar/intensity_bar_ui.c",
             UI / "components/menu_component/menu_component_ui.c"):
    code += f'\n#include "{path}"\n'
code += r"""
static lv_timer_t *maybe_timer(lv_timer_cb_t callback, uint32_t period, void *context) {
  return timer_fails ? NULL : lv_timer_create(callback, period, context);
}
#define lv_timer_create maybe_timer
"""
code += f'\n#include "{UI / "screens/wifi/wifi_port_scan_ui.c"}"\n'
code += r"""
#undef lv_timer_create
static uint16_t pixels[320*320];
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *buffer) {
  uint16_t *input=(uint16_t *)buffer;
  for(int y=area->y1;y<=area->y2;++y) for(int x=area->x1;x<=area->x2;++x)
    pixels[y*ui_screen_w()+x]=*input++;
  lv_display_flush_ready(display);
}
static void capture(const char *stage,unsigned wide,unsigned theme) {
  lv_obj_invalidate(s_screen); lv_refr_now(NULL);
  char name[64]; snprintf(name,sizeof(name),"%s_%u_%u.ppm",stage,wide,theme);
  FILE *file=fopen(name,"wb"); assert(file); fprintf(file,"P6\n%d %d\n255\n",ui_screen_w(),ui_screen_h());
  for(int i=0;i<ui_screen_w()*ui_screen_h();++i) {
    uint16_t p=pixels[i]; unsigned char rgb[]={((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};
    fwrite(rgb,1,3,file);
  }
  fclose(file);
}
static void inside(lv_obj_t *obj) {
  lv_area_t area; lv_obj_get_coords(obj,&area);
  assert(area.x1>=0 && area.x2<ui_screen_w() && area.y1>=UI_CHROME_HEADER_H);
  assert(area.y2<ui_screen_h()-UI_CHROME_FOOTER_H);
}
static void check_execution(void) {
  lv_obj_update_layout(s_screen); assert(s_run_rows==(ui_screen_w()==240?4:2));
  lv_obj_t *items[]={s_run_phase,s_run_ip,s_run_progress,s_run_storage,s_run_bar};
  for(unsigned i=0;i<sizeof(items)/sizeof(items[0]);++i) inside(items[i]);
  for(unsigned i=0;i<s_run_count;++i) {
    inside(s_run_items[i]); inside(s_run_titles[i]); inside(s_run_banners[i]);
    lv_area_t row,banner; lv_obj_get_coords(s_run_items[i],&row); lv_obj_get_coords(s_run_banners[i],&banner);
    assert(banner.y2<=row.y2 && banner.x2<=row.x2);
  }
  assert(lv_color_eq(lv_obj_get_style_text_color(s_run_ip,0),current_theme.text_main));
  assert(lv_color_eq(lv_obj_get_style_bg_color(s_screen,0),current_theme.screen_base));
}
static void send(int button) { input_event_t event={INPUT_ACTION_PRESS,button}; input(&event,NULL); }
static void check_layout(void) {
  for(int i=0;i<s_menu.item_count;++i) {
    const char *label=lv_label_get_text(lv_obj_get_child(s_menu.items[i],0));
    assert(strcmp(label,"Previous page") && strcmp(label,"Next page"));
  }
  lv_obj_scroll_to_view(s_menu.items[s_menu.selected], LV_ANIM_OFF);
  lv_obj_update_layout(s_screen);
  lv_obj_t *objects[]={s_menu.items[s_menu.selected],s_menu.hint_label};
  for(unsigned i=0;i<2;++i) {
    lv_area_t area; lv_obj_get_coords(objects[i],&area);
    assert(area.x1>=0 && area.x2<ui_screen_w()); assert(area.y1>=46 && area.y2<ui_screen_h());
  }
  assert(lv_color_eq(lv_obj_get_style_bg_color(s_screen,0),current_theme.screen_base));
}
static void select_row(unsigned row) { menu_component_select(&s_menu,row); check_layout(); }
static void close_screen(lv_obj_t *blank) {
  ui_wifi_port_scan_close(); assert(!keyboard_visible && s_timer==NULL);
  lv_screen_load(blank); lv_obj_delete(s_screen); assert(s_screen==NULL);
}
static void finish_page(unsigned total, int error) {
  mock_page.is_busy=false; mock_page.total=total; mock_page.error=error;
  uint64_t first=(uint64_t)mock_page.number*PORT_SCANNER_PAGE_SIZE;
  mock_page.count=first<total ? (total-first>PORT_SCANNER_PAGE_SIZE?PORT_SCANNER_PAGE_SIZE:total-first) : 0;
  mock_page.has_next=first+mock_page.count<total;
  mock_page.cursor=first; mock_page.next_cursor=first+mock_page.count;
  for(unsigned i=0;i<mock_page.count;++i) {
    mock_page.hosts[i]=(port_scanner_host_t){.ip="192.168.255.254",.first_record=0,.count=70000};
    snprintf(mock_page.hosts[i].ip,sizeof(mock_page.hosts[i].ip),"192.168.0.%u",(unsigned)((first+i)%254)+1);
    snprintf(mock_page.labels[i],sizeof(mock_page.labels[i]),"192.168.%u.0/24",i);
    mock_page.results[i]=(port_scan_result_t){.ip_str="192.168.255.254",.port=65535,.banner="SSH-2.0-example"};
    if(requested_browse.kind==PORT_SCANNER_BROWSE_PORTS)
      strcpy(mock_page.results[i].ip_str,requested_browse.host.ip);
    snprintf(mock_page.files[i],sizeof(mock_page.files[i]),"scan_20261123_123456_%08x.csv",i);
  }
  refresh(NULL);
}
static void set_text(const char *text) { assert(keyboard_visible); keyboard_close(); submit_cb(text,submit_context); }
int main(void) {
  lv_init(); lv_display_t *display=lv_display_create(240,320);
  _Alignas(LV_DRAW_BUF_ALIGN) static uint8_t buffer[320*20*2];
  lv_display_set_buffers(display,buffer,NULL,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(display,flush); lv_obj_t *blank=lv_screen_active();
  for(unsigned wide=0;wide<2;++wide) {
    lv_display_set_resolution(display,wide?320:240,wide?240:320);
    for(unsigned theme=0;theme<2;++theme) {
      current_theme.screen_base=lv_color_hex(theme?0xffffff:0x101010);
      current_theme.bg_primary=current_theme.bg_secondary=current_theme.screen_base;
      current_theme.text_main=lv_color_hex(theme?0x101010:0xffffff);
      current_theme.border_accent=lv_color_hex(theme?0x6936a8:0xb29aff);
      current_theme.text_secondary=lv_color_hex(theme?0x555555:0xaaaaaa);
      current_theme.bg_secondary=lv_color_hex(theme?0xe7e1f0:0x25212c);
      current_theme.bg_item_top=current_theme.bg_item_bot=current_theme.bg_secondary;
      current_theme.border_inactive=lv_color_hex(theme?0xcccccc:0x333333);
      mock_page=(port_scanner_page_t){0};
      mock_status=(port_scanner_snapshot_t){0}; s_config.target_mode=PORT_SCANNER_GATEWAY;
      s_config.port_mode=PORT_SCANNER_COMMON_PORTS; s_config.is_ping_enabled=false;
      unsigned before=requests;
      root_fails=true; ui_wifi_port_scan_open(); assert(!s_is_open && s_timer==NULL);
      unsigned old_backs=backs; send(INPUT_BTN_BACK); assert(backs==old_backs+1); root_fails=false;
      timer_fails=true; ui_wifi_port_scan_open(); assert(s_timer==NULL);
      select_row(WIFI_PORT_SCAN_START); send(INPUT_BTN_OK); assert(requests==before);
      unsigned old_page_requests=page_requests;
      select_row(WIFI_PORT_SCAN_SAVED); send(INPUT_BTN_OK); assert(page_requests==old_page_requests);
      close_screen(blank); timer_fails=false;
      ui_wifi_port_scan_open(); assert(requests==before);
      for(unsigned row=0;row<WIFI_PORT_SCAN_COUNT;++row) select_row(row);
      select_row(WIFI_PORT_SCAN_TARGET); send(INPUT_BTN_RIGHT); assert(s_config.target_mode==PORT_SCANNER_HOST);
      send(INPUT_BTN_RIGHT); assert(s_config.target_mode==PORT_SCANNER_LOCAL);
      send(INPUT_BTN_RIGHT); assert(s_config.target_mode==PORT_SCANNER_RANGE);
      send(INPUT_BTN_RIGHT); assert(s_config.target_mode==PORT_SCANNER_CIDR);
      select_row(WIFI_PORT_SCAN_ADDRESS); send(INPUT_BTN_OK); set_text("10.23.0.0/16");
      assert(!strcmp(s_config.target,"10.23.0.0/16"));
      send(INPUT_BTN_OK); set_text("invalid"); assert(!strcmp(s_config.target,"10.23.0.0/16"));
      select_row(WIFI_PORT_SCAN_PORTS); send(INPUT_BTN_RIGHT); assert(s_config.port_mode==PORT_SCANNER_CUSTOM_PORTS);
      select_row(WIFI_PORT_SCAN_LIST); send(INPUT_BTN_OK); set_text("22,80,443,8000-8100");
      select_row(WIFI_PORT_SCAN_PROTOCOL); send(INPUT_BTN_RIGHT); assert(s_config.protocol_mode==PORT_SCANNER_UDP);
      send(INPUT_BTN_RIGHT); assert(s_config.protocol_mode==PORT_SCANNER_BOTH);
      send(INPUT_BTN_RIGHT); assert(s_config.protocol_mode==PORT_SCANNER_TCP);
      send(INPUT_BTN_LEFT); assert(s_config.protocol_mode==PORT_SCANNER_BOTH);
      send(INPUT_BTN_RIGHT); assert(s_config.protocol_mode==PORT_SCANNER_TCP);
      select_row(WIFI_PORT_SCAN_PING); send(INPUT_BTN_OK); assert(s_config.is_ping_enabled);
      select_row(WIFI_PORT_SCAN_START); send(INPUT_BTN_OK); assert(requests==before+1 && s_is_execution);
      assert(last_request.is_ping_enabled && last_request.target_mode==PORT_SCANNER_CIDR);
      assert(!strcmp(last_request.ports,"22,80,443,8000-8100")); check_execution();
      mock_status.hosts_total=65534; mock_status.hosts_probed=32768;
      mock_status.hosts_responded=8192; mock_status.hosts_skipped=24576;
      mock_status.hosts_done=24576; mock_status.ports_total=65535;
      mock_status.is_probing=true; mock_status.probe_count=8;
      strcpy(mock_status.current_ip,"10.23.128.1"); refresh(NULL); check_execution();
      assert(strstr(lv_label_get_text(s_run_phase),"1/2 DISCOVERY"));
      assert(!strcmp(lv_label_get_text(s_run_progress),"32768/65534 IPs   8192 up"));
      assert(strstr(lv_label_get_text(s_run_storage),"PING x8"));
      assert(lv_bar_get_value(s_run_bar)==500 && s_run_count==0);
      capture("discovery",wide,theme);
      mock_status.state=PORT_SCANNER_CANCELLED; mock_status.is_busy=false;
      mock_status.is_probing=false; refresh(NULL); check_execution();
      assert(lv_bar_get_value(s_run_bar)==500);
      assert(!strcmp(lv_label_get_text(s_run_empty),"Port scan not started"));
      mock_status.is_discovery_complete=true; mock_status.hosts_responded=0;
      mock_status.hosts_done=mock_status.hosts_skipped=65534;
      mock_status.state=PORT_SCANNER_DONE; refresh(NULL); check_execution();
      assert(lv_bar_get_value(s_run_bar)==1000);
      assert(!strcmp(lv_label_get_text(s_run_empty),"No hosts replied to ping"));
      mock_status.state=PORT_SCANNER_RUNNING; mock_status.is_busy=true;
      mock_status.hosts_total=254; mock_status.hosts_done=17; mock_status.ports_total=1024;
      mock_status.hosts_probed=254; mock_status.hosts_responded=244;
      mock_status.ports_done=512; mock_status.found=150; mock_status.stored=128;
      mock_status.saved=150; mock_status.hosts_skipped=10;
      strcpy(mock_status.current_ip,"192.168.0.18"); refresh(NULL); check_execution();
      assert(strstr(lv_label_get_text(s_run_phase),"2/2 TCP SCAN   7/244 hosts"));
      capture("scan",wide,theme);
      udp_results=true; mock_status.active_protocol=PORT_SCAN_PROTO_UDP;
      mock_status.uncertain=2; mock_status.filtered=3;
      for(unsigned state=PORT_SCAN_STATUS_OPEN_FILTERED;state<=PORT_SCAN_STATUS_FILTERED;++state) {
        udp_state=state; refresh(NULL); check_execution();
        assert(strstr(lv_label_get_text(s_run_phase),"UDP SCAN"));
        assert(strstr(lv_label_get_text(s_run_storage),"2 uncertain   3 filtered"));
        assert(strstr(lv_label_get_text(s_run_titles[0]),"/UDP"));
        assert(strstr(lv_label_get_text(s_run_banners[0]),state==PORT_SCAN_STATUS_OPEN_FILTERED ? "OPEN|FILTERED" : "FILTERED:"));
        send(INPUT_BTN_OK); capture(state==PORT_SCAN_STATUS_OPEN_FILTERED ? "udp_uncertain" : "udp_filtered",wide,theme);
      }
      udp_results=false; mock_status.active_protocol=PORT_SCAN_PROTO_TCP;
      mock_status.uncertain=mock_status.filtered=0; refresh(NULL);
      long_banner=true; refresh(NULL); check_execution(); long_banner=false; refresh(NULL);
      assert(s_run_first==128-s_run_rows && s_run_count==s_run_rows);
      unsigned previous_selected=s_run_selected; send(INPUT_BTN_UP);
      assert(!s_is_following_latest && s_run_selected==previous_selected-1);
      send(INPUT_BTN_OK); assert(details>0);
      lv_color_t old_color=current_theme.text_main; current_theme.text_main=current_theme.text_secondary;
      refresh(NULL); check_execution(); current_theme.text_main=old_color; refresh(NULL);
      mock_status.hosts_total=1; mock_status.hosts_done=0; mock_status.ports_total=100;
      mock_status.hosts_skipped=0; mock_status.hosts_responded=1;
      mock_status.ports_done=50; mock_status.is_probing=false; refresh(NULL);
      assert(lv_bar_get_value(s_run_bar)==500);
      mock_status.is_ping_enabled=false; refresh(NULL); assert(lv_bar_get_value(s_run_bar)==500);
      mock_status.state=PORT_SCANNER_FAILED; mock_status.error=ESP_FAIL; mock_status.is_busy=false;
      refresh(NULL); check_execution(); assert(strstr(lv_label_get_text(s_run_phase),"Failed"));
      mock_status.state=PORT_SCANNER_RUNNING; mock_status.error=ESP_OK; mock_status.is_busy=true;
      unsigned previous=stop_requests; send(INPUT_BTN_BACK);
      assert(s_is_execution && stop_requests==previous+1 && mock_status.state==PORT_SCANNER_STOPPING);
      send(INPUT_BTN_BACK); assert(!s_is_execution && mock_status.is_busy);
      select_row(WIFI_PORT_SCAN_START); send(INPUT_BTN_OK); assert(requests==before+1);
      close_screen(blank); mock_status.is_busy=false; mock_status.state=PORT_SCANNER_CANCELLED;
      ui_wifi_port_scan_open(); assert(mock_status.stored==128); select_row(WIFI_PORT_SCAN_RESULTS);
      send(INPUT_BTN_OK); assert(s_page_count==8); send(INPUT_BTN_BACK);
      strcpy(mock_status.archive_path,"/sdcard/wifi/port_scans/scan_20261123_123456_00000001.csv");
      mock_status.saved=70000; mock_status.found=70000;
      select_row(WIFI_PORT_SCAN_RESULTS); send(INPUT_BTN_OK); assert(s_is_loading && s_use_sd_results);
      finish_page(17,ESP_OK); assert(s_show_hosts && s_page_count==8);
      assert(s_menu.item_count==9); select_row(0); check_layout(); capture("hosts",wide,theme);
      unsigned old_requests=page_requests;
      send(INPUT_BTN_LEFT); assert(page_requests==old_requests);
      send(INPUT_BTN_RIGHT); assert(s_page==1 && requested_browse.cursor==8);
      finish_page(17,ESP_OK); send(INPUT_BTN_RIGHT); finish_page(17,ESP_OK);
      assert(s_page_count==1 && s_page==2);
      old_requests=page_requests; send(INPUT_BTN_RIGHT); assert(page_requests==old_requests);
      send(INPUT_BTN_LEFT); assert(requested_browse.is_previous && s_page==1);
      finish_page(17,ESP_OK); select_row(0); send(INPUT_BTN_OK);
      assert(!s_show_hosts && requested_browse.kind==PORT_SCANNER_BROWSE_PORTS);
      finish_page(70000,ESP_OK); assert(s_menu.item_count==8); select_row(0); capture("ports",wide,theme); send(INPUT_BTN_OK);
      s_page=8192; request_page(); finish_page(70000,ESP_OK);
      assert(mock_page.number==8192 && s_page_count==8); select_row(0); check_layout();
      send(INPUT_BTN_RIGHT); finish_page(70000,ESP_OK); assert(s_page==8193);
      send(INPUT_BTN_LEFT); finish_page(70000,ESP_OK); assert(s_page==8192);
      send(INPUT_BTN_BACK); assert(s_show_hosts && s_page==1 && requested_browse.cursor==8);
      finish_page(17,ESP_OK); send(INPUT_BTN_BACK); assert(!s_show_results && !s_show_files);
      select_row(WIFI_PORT_SCAN_SAVED); send(INPUT_BTN_OK); assert(s_show_files && s_is_loading);
      finish_page(9,ESP_OK); assert(s_menu.item_count==8); select_row(0); capture("networks",wide,theme); send(INPUT_BTN_OK);
      assert(s_use_sd_results && s_show_hosts && s_is_loading && strstr(requested_path,"scan_"));
      // BACK while the old read is pending defers the directory request until it exits.
      send(INPUT_BTN_BACK); assert(s_show_files && s_is_loading && s_is_page_deferred);
      finish_page(17,ESP_OK); assert(s_is_loading && requested_path[0]=='\0');
      finish_page(9,ESP_OK); assert(s_page_count==8); select_row(0); check_layout();
      send(INPUT_BTN_RIGHT); finish_page(9,ESP_OK);
      assert(s_page_count==1 && s_page==1); select_row(0); check_layout();
      send(INPUT_BTN_OK); finish_page(0,ESP_FAIL); assert(s_page_count==0); select_row(0); check_layout();
      send(INPUT_BTN_BACK); finish_page(9,ESP_OK);
      select_row(0); send(INPUT_BTN_OK); finish_page(1,ESP_OK);
      select_row(s_page_count); send(INPUT_BTN_OK); assert(confirm_cb && s_is_delete_confirm);
      old_requests=delete_requests; confirm_cb(false); assert(delete_requests==old_requests);
      send(INPUT_BTN_OK); confirm_cb(true); assert(s_is_deleting && delete_requests==old_requests+1);
      finish_page(0,ESP_FAIL); assert(s_show_hosts && s_is_loading && !s_is_deleting);
      finish_page(1,ESP_OK); select_row(s_page_count); send(INPUT_BTN_OK); confirm_cb(true);
      finish_page(0,ESP_OK); assert(s_show_files && s_is_loading);
      finish_page(8,ESP_OK); assert(s_page==0 && s_is_loading); // Last directory page disappeared.
      finish_page(8,ESP_OK); send(INPUT_BTN_BACK);
      mock_status.storage_error=ESP_FAIL; refresh(NULL); select_row(WIFI_PORT_SCAN_START); check_layout();
      // Screen teardown cancels an outstanding reader; no callback owns a UI object.
      select_row(WIFI_PORT_SCAN_SAVED); send(INPUT_BTN_OK); assert(s_is_loading);
      unsigned old_closes=page_closes; close_screen(blank); assert(page_closes>old_closes);
      mock_page.is_busy=false; ui_wifi_port_scan_open();
      select_row(WIFI_PORT_SCAN_SAVED); send(INPUT_BTN_OK); finish_page(1,ESP_OK);
      select_row(0); send(INPUT_BTN_OK); finish_page(0,ESP_OK);
      assert(s_show_hosts && s_menu.item_count==2); select_row(1); send(INPUT_BTN_OK);
      assert(s_is_delete_confirm); close_screen(blank); assert(!confirm_cb && !s_is_delete_confirm);
      ui_wifi_port_scan_open();
      select_row(WIFI_PORT_SCAN_ADDRESS); send(INPUT_BTN_OK); assert(keyboard_visible); close_screen(blank);
    }
  }
  puts("PASS: real LVGL scanner, network/IP/port hierarchy, LEFT/RIGHT paging, checked deletion and failure/retry, cancellation/exit, themes, orientations, archive errors and pending-read cancellation");
}
"""
test = WORK / "test.c"
test.write_text(NOTICE + code)
command = [os.environ.get("CC", "cc"), "-std=gnu11", "-g", "-fsanitize=undefined,bounds", "-fno-sanitize-recover=all",
           "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_KCONFIG_IGNORE", "-I" + str(WORK), "-I" + str(BASE)]
for folder in ("screens/wifi", "components/keyboard", "components/menu_component", "components/toggle", "components/intensity_bar"):
    command += ["-I" + str(UI / folder / "include")]
command += ["-I" + str(SERVICE / "port_scanner/include"),
            "-I" + str(ROOT / "firmware_p4/components/Applications/wifi/include"),
            "-I" + str(ROOT / "firmware_p4/managed_components/lvgl__lvgl"), str(test),
            str(BASE / "build/lvgl-build/lib/liblvgl.a"), "-lm", "-o", str(WORK / "test")]
subprocess.run(command, check=True, timeout=60)
subprocess.run([str(WORK / "test")], cwd=WORK, check=True, timeout=30)
