// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include "lora_chat_ui.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "assets_manager.h"
#include "keyboard_ui.h"
#include "lora_session.h"
#include "menu_component_ui.h"
#include "meshtastic_presets.h"
#include "meshtastic_regions.h"
#include "meshtastic_roles.h"
#include "notify_ui.h"
#include "st7789.h"
#include "sys_prio.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_semantic.h"
#include "ui_theme.h"
#include "waves_ui.h"

static const char *TAG = "LORA_MESH";

#define CONNECT_POLL_MS       1000
#define CHAT_POLL_MS          500
#define CHAT_BATCH            8
#define LORA_START_TASK_STACK 12288
#define ENTRY_MS              220

#define BADGE_SIZE    28
#define BADGE_ICON_PX 18

#define DOTS_STEP_MS 340
#define DOTS_MAX     3

#define BUBBLE_MAX_W 184

#define LORA_AMBER     0xF5B13D
#define MAP_CENTER_X   120
#define MAP_CENTER_Y   150
#define YOU_PIN_SZ     32
#define MESH_CAP_Y     46
#define MESH_INFO_W    210
#define MESH_INFO_H    36
#define MESH_INFO_BOT  26
#define MESH_GLOW_W    16
#define SNR_RSSI_FLOOR -100
#define SNR_RSSI_SPAN  70
#define RSSI_STRONG    -55
#define RSSI_GOOD      -70

#define LIST_ROW_H       44
#define LIST_ROW_RADIUS  10
#define LIST_ROW_PAD_H   10
#define LIST_ROW_COL_GAP 10
#define LIST_PAD_SIDE    8
#define LIST_ROW_GAP     6
#define LIST_SB_W        4
#define LIST_SB_RADIUS   2
#define LIST_ROW_GLOW_W  14
#define LIST_SLOT_W      28
#define LIST_DOT_SIZE    12
#define LIST_DOT_GLOW_W  8

static const char *PROTOS[] = {"MeshCore", "Meshtastic"};
#define PROTO_COUNT ((int)(sizeof(PROTOS) / sizeof(PROTOS[0])))

static const char *PROTO_ICONS[] = {"/assets/icons/hub.bin", "/assets/icons/lan.bin"};
static const char *PROTO_BANDS[] = {"868 MHz", "915 MHz"};

#define NODE_COUNT 5

typedef struct {
  char name[32];
  int rssi;
  bool strong;
} node_row_t;

static node_row_t s_nodes[NODE_COUNT];
static int s_rnode_count = 0;

static const struct {
  int x;
  int y;
} NODE_POS[NODE_COUNT] = {
    {62, 90},
    {178, 94},
    {56, 206},
    {186, 198},
    {120, 234},
};

enum { CFG_REGION = 0, CFG_PRESET, CFG_ROUTER, CFG_COUNT };

typedef enum {
  VIEW_PROTO = 0,
  VIEW_HOME,
  VIEW_CONNECT,
  VIEW_NODES,
  VIEW_CHAT,
  VIEW_CONFIGS,
} view_t;

static lv_obj_t *s_screen = NULL;
static menu_component_t s_menu;
static lv_obj_t *s_chat_list = NULL;
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_hint = NULL;
static lv_timer_t *s_connect_timer = NULL;
static lv_timer_t *s_chat_poll = NULL;
static view_t s_view = VIEW_PROTO;
static int s_proto = 0;
static int s_home_sel = 0;
static int s_node = 0;
static bool s_linked = false;
static bool s_starting = false;
static int s_cfg_region = 0, s_cfg_preset = 0;
static uint32_t s_chat_seq = 0;

static lv_obj_t *s_node_pins[NODE_COUNT];
static lv_obj_t *s_node_names[NODE_COUNT];
static lv_obj_t *s_info_name = NULL;
static lv_obj_t *s_info_meta = NULL;
static lv_obj_t *s_info_bar = NULL;
static lv_point_precise_t s_link_pts[NODE_COUNT][2];

static lv_obj_t *s_node_rows[NODE_COUNT];
static lv_obj_t *s_list_name[NODE_COUNT];
static lv_obj_t *s_list_val[NODE_COUNT];
static lv_obj_t *s_node_list = NULL;
static bool s_nodes_list = false;
static bool s_nodes_ok_fired = false;
static bool s_nodes_ok_active = false;

static void lora_chat_input(const input_event_t *ev, void *ctx);
static void build_screen(void);
static void add_bubble(bool outgoing, const char *who, const char *text);

static void stop_timers(void) {
  if (s_connect_timer != NULL) {
    lv_timer_delete(s_connect_timer);
    s_connect_timer = NULL;
  }
  if (s_chat_poll != NULL) {
    lv_timer_delete(s_chat_poll);
    s_chat_poll = NULL;
  }
}

static lora_proto_t proto_for_sel(int sel) {
  return (sel == 0) ? LORA_PROTO_MESHCORE : LORA_PROTO_MESHTASTIC;
}

static int sel_for_proto(lora_proto_t proto) {
  return (proto == LORA_PROTO_MESHTASTIC) ? 1 : 0;
}

static void lora_start_task(void *pv) {
  lora_proto_t proto = (lora_proto_t)(intptr_t)pv;
  esp_err_t err = lora_session_start(proto);
  if (err != ESP_OK)
    ESP_LOGE(TAG, "start proto %d: %s", (int)proto, esp_err_to_name(err));
  s_starting = false;
  vTaskDelete(NULL);
}

static void start_active_proto(lora_proto_t proto) {
  if (s_starting || lora_session_active() != LORA_PROTO_NONE)
    return;
  s_starting = true;
  if (xTaskCreatePinnedToCore(lora_start_task,
                              "lora_start",
                              LORA_START_TASK_STACK,
                              (void *)(intptr_t)proto,
                              SYS_PRIO_BACKGROUND,
                              NULL,
                              SYS_CORE_RADIO) != pdPASS) {
    s_starting = false;
    ESP_LOGE(TAG, "failed to spawn start task");
  }
}

static void refresh_nodes(void) {
  uint16_t total = lora_session_node_count();
  int n = 0;
  for (uint16_t i = 0; i < total && n < NODE_COUNT; i++) {
    lora_node_t nd;
    if (!lora_session_node_get(i, &nd))
      continue;
    snprintf(
        s_nodes[n].name, sizeof(s_nodes[n].name), "%s", (nd.name[0] != '\0') ? nd.name : "node");
    s_nodes[n].rssi = nd.rssi;
    s_nodes[n].strong = (nd.rssi == 0) || (nd.rssi >= RSSI_GOOD);
    n++;
  }
  s_rnode_count = n;
  if (s_node >= s_rnode_count)
    s_node = (s_rnode_count > 0) ? s_rnode_count - 1 : 0;
}

static void fmt_rssi(int rssi, char *buf, size_t n) {
  if (rssi == 0)
    snprintf(buf, n, "-- dBm");
  else
    snprintf(buf, n, "%d dBm", rssi);
}

static void transy_cb(void *var, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void card_rise(lv_obj_t *o) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, transy_cb);
  lv_anim_set_values(&a, 26, 0);
  lv_anim_set_duration(&a, 300);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static lv_obj_t *lit_panel(lv_obj_t *parent, int w, int h, lv_color_t accent) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(p, w, h);
  lv_obj_set_style_radius(p, 13, 0);
  lv_obj_set_style_bg_color(p, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(p, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(p, 1, 0);
  lv_obj_set_style_border_color(p, accent, 0);
  lv_obj_set_style_shadow_color(p, accent, 0);
  lv_obj_set_style_shadow_width(p, 14, 0);
  lv_obj_set_style_shadow_opa(p, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(p, -3, 0);
  return p;
}

static lv_obj_t *make_badge(lv_obj_t *parent, const char *path, lv_color_t accent) {
  lv_obj_t *badge = lv_obj_create(parent);
  lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(badge, BADGE_SIZE, BADGE_SIZE);
  lv_obj_set_style_radius(badge, 8, 0);
  lv_obj_set_style_pad_all(badge, 0, 0);
  lv_obj_set_style_bg_color(badge, current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(badge, 1, 0);
  lv_obj_set_style_border_color(badge, accent, 0);

  lv_image_dsc_t *dsc = assets_get(path);
  if (dsc != NULL) {
    lv_obj_t *img = lv_image_create(badge);
    lv_image_set_src(img, dsc);
    int32_t longest = dsc->header.w > dsc->header.h ? dsc->header.w : dsc->header.h;
    if (longest > 0)
      lv_image_set_scale(img, BADGE_ICON_PX * 256 / longest);
    lv_obj_set_style_image_recolor(img, current_theme.text_main, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
    lv_obj_center(img);
  }
  return badge;
}

static void build_proto_view(void) {
  s_menu = menu_component_create(s_screen, "SELECT NETWORK", "/assets/icons/lan.bin");
  menu_component_add_section(&s_menu, "MESH PROTOCOL");
  for (int i = 0; i < PROTO_COUNT; i++) {
    char label[48];
    snprintf(label, sizeof(label), "%s   %s", PROTOS[i], PROTO_BANDS[i]);
    menu_component_add_item(&s_menu, PROTO_ICONS[i], label);
  }
  menu_component_select(&s_menu, s_proto);
  menu_component_set_hint(&s_menu, LV_SYMBOL_UP LV_SYMBOL_DOWN "  choose   OK enter   BACK exit");
}

static void build_home_view(void) {
  s_menu = menu_component_create(s_screen, PROTOS[s_proto], "/assets/icons/hub.bin");

  if (lora_session_active() == LORA_PROTO_MESHTASTIC) {
    const mt_region_info_t *rg = mt_region_info(mt_region_current());
    const mt_preset_info_t *pr = mt_preset_info(mt_preset_current());
    char band[64];
    snprintf(
        band, sizeof(band), "%s  %s", (rg != NULL) ? rg->name : "?", (pr != NULL) ? pr->name : "?");
    menu_component_add_section(&s_menu, band);
  }
  menu_component_add_section(&s_menu, s_linked ? "[ MESH ONLINE ]" : "[ STANDALONE ]");

  menu_component_add_item(&s_menu, "/assets/icons/bluetooth.bin", "Connect App");
  menu_component_add_item(&s_menu, "/assets/icons/hub.bin", "Nodes");
  menu_component_add_item(&s_menu, "/assets/icons/tune.bin", "Configs");
  menu_component_add_item(&s_menu, "/assets/icons/swap_horiz.bin", "Channels");
  menu_component_add_item(&s_menu, "/assets/icons/sensors.bin", "Position");
  menu_component_add_item(&s_menu, "/assets/icons/monitoring.bin", "Telemetry");
  menu_component_add_item(&s_menu, "/assets/icons/vpn_key.bin", "Secure DM");
  menu_component_add_item(&s_menu, "/assets/icons/lan.bin", "Traceroute");

  if (s_linked)
    menu_component_set_item_label_color(&s_menu, 0, lv_color_hex(UI_COL_SUCCESS));

  menu_component_select(&s_menu, s_home_sel);
  menu_component_set_hint(&s_menu,
                          LV_SYMBOL_UP LV_SYMBOL_DOWN "  open   OK enter   BACK protocols");
}

static void link_style(int i, lv_color_t *col, int *w, bool *dashed) {
  if (!s_nodes[i].strong) {
    *col = current_theme.text_secondary;
    *w = 1;
    *dashed = true;
  } else if (s_nodes[i].rssi >= RSSI_STRONG) {
    *col = lv_color_hex(UI_COL_SUCCESS);
    *w = 3;
    *dashed = false;
  } else if (s_nodes[i].rssi >= RSSI_GOOD) {
    *col = lv_color_hex(UI_COL_SUCCESS);
    *w = 2;
    *dashed = false;
  } else {
    *col = lv_color_hex(LORA_AMBER);
    *w = 1;
    *dashed = false;
  }
}

static void node_style_pin(int i, bool selected) {
  lv_obj_t *pin = s_node_pins[i];
  if (pin == NULL)
    return;
  bool online = s_nodes[i].strong;
  lv_color_t edge = selected ? current_theme.border_accent
                             : (online ? lv_color_hex(UI_COL_SUCCESS) : current_theme.text_secondary);
  lv_obj_set_style_border_color(pin, edge, 0);
  lv_obj_set_style_border_width(pin, selected ? 2 : 1, 0);
  lv_obj_set_style_opa(pin, (online || selected) ? LV_OPA_COVER : LV_OPA_50, 0);
  if (selected) {
    lv_obj_set_style_shadow_color(pin, edge, 0);
    lv_obj_set_style_shadow_width(pin, MESH_GLOW_W, 0);
    lv_obj_set_style_shadow_opa(pin, LV_OPA_40, 0);
    lv_obj_set_style_shadow_spread(pin, -2, 0);
  } else {
    lv_obj_set_style_shadow_width(pin, 0, 0);
    lv_obj_set_style_shadow_opa(pin, LV_OPA_TRANSP, 0);
  }
  if (s_node_names[i] != NULL)
    lv_obj_set_style_text_color(s_node_names[i],
                                selected
                                    ? current_theme.border_accent
                                    : (online ? current_theme.text_main : current_theme.text_secondary),
                                0);
}

static void node_update_info(int i) {
  if (i >= s_rnode_count)
    return;
  bool online = s_nodes[i].strong;
  if (s_info_name != NULL) {
    lv_label_set_text(s_info_name, s_nodes[i].name);
    lv_obj_set_style_text_color(
        s_info_name, online ? current_theme.text_main : current_theme.text_secondary, 0);
  }
  if (s_info_meta != NULL) {
    char meta[40];
    char rssi_s[16];
    fmt_rssi(s_nodes[i].rssi, rssi_s, sizeof(rssi_s));
    snprintf(meta, sizeof(meta), "%s \xC2\xB7 %s", rssi_s, online ? "ONLINE" : "OFFLINE");
    lv_label_set_text(s_info_meta, meta);
    lv_obj_set_style_text_color(
        s_info_meta, online ? lv_color_hex(UI_COL_SUCCESS) : current_theme.text_secondary, 0);
  }
  if (s_info_bar != NULL) {
    int pct = (s_nodes[i].rssi - SNR_RSSI_FLOOR) * 100 / SNR_RSSI_SPAN;
    if (pct < 5)
      pct = 5;
    if (pct > 100)
      pct = 100;
    lv_bar_set_value(s_info_bar, pct, LV_ANIM_OFF);
    lv_color_t col;
    int w;
    bool dashed;
    link_style(i, &col, &w, &dashed);
    (void)w;
    (void)dashed;
    lv_obj_set_style_bg_color(s_info_bar, col, LV_PART_INDICATOR);
  }
}

static void node_style_row(int i, bool selected) {
  lv_obj_t *row = s_node_rows[i];
  if (row == NULL)
    return;
  lv_obj_set_style_bg_color(
      row, selected ? current_theme.bg_secondary : current_theme.bg_primary, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_border_color(
      row, selected ? current_theme.border_accent : current_theme.border_inactive, 0);
  if (selected) {
    lv_obj_set_style_shadow_color(row, current_theme.border_accent, 0);
    lv_obj_set_style_shadow_width(row, LIST_ROW_GLOW_W, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_40, 0);
    lv_obj_set_style_shadow_spread(row, -2, 0);
  } else {
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_shadow_opa(row, LV_OPA_TRANSP, 0);
  }
  if (s_list_name[i] != NULL)
    lv_obj_set_style_text_color(
        s_list_name[i], selected ? current_theme.text_main : current_theme.text_secondary, 0);
  if (s_list_val[i] != NULL)
    lv_obj_set_style_text_color(
        s_list_val[i], selected ? current_theme.border_accent : current_theme.text_secondary, 0);
}

static void node_select(int sel) {
  if (s_nodes_list) {
    for (int i = 0; i < NODE_COUNT; i++)
      node_style_row(i, i == sel);
    if (s_node_list != NULL && s_node_rows[sel] != NULL) {
      lv_obj_update_layout(s_node_list);
      lv_obj_scroll_to_view(s_node_rows[sel], LV_ANIM_ON);
    }
    return;
  }
  for (int i = 0; i < NODE_COUNT; i++)
    node_style_pin(i, i == sel);
  node_update_info(sel);
}

static void build_nodes_list(void) {
  int online = 0;
  for (int i = 0; i < s_rnode_count; i++)
    if (s_nodes[i].strong)
      online++;

  s_node_list = lv_obj_create(s_screen);
  lv_obj_set_size(s_node_list, lv_pct(100), LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(s_node_list, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(s_node_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_node_list, 0, 0);
  lv_obj_set_style_pad_all(s_node_list, 0, 0);
  lv_obj_set_style_pad_left(s_node_list, LIST_PAD_SIDE, 0);
  lv_obj_set_style_pad_right(s_node_list, LIST_PAD_SIDE, 0);
  lv_obj_set_style_pad_row(s_node_list, LIST_ROW_GAP, 0);
  lv_obj_set_flex_flow(s_node_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(s_node_list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_node_list, LV_SCROLLBAR_MODE_ON);
  lv_obj_remove_flag(s_node_list, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_remove_flag(s_node_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_style_bg_color(s_node_list, current_theme.border_accent, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(s_node_list, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_width(s_node_list, LIST_SB_W, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(s_node_list, LIST_SB_RADIUS, LV_PART_SCROLLBAR);

  lv_obj_t *cap = lv_label_create(s_node_list);
  lv_label_set_text_fmt(cap, "%d / %d s_nodes ONLINE", online, s_rnode_count);
  lv_obj_set_style_text_color(cap, current_theme.text_secondary, 0);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);

  if (s_rnode_count == 0) {
    lv_obj_t *empty = lv_label_create(s_node_list);
    lv_label_set_text(empty, "Listening for nodes...");
    lv_obj_set_style_text_color(empty, current_theme.text_secondary, 0);
    lv_obj_set_style_text_font(empty, &lv_font_montserrat_14, 0);
  }

  for (int i = 0; i < s_rnode_count; i++) {
    bool node_online = s_nodes[i].strong;

    lv_obj_t *row = lv_obj_create(s_node_list);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LIST_ROW_H);
    lv_obj_set_style_radius(row, LIST_ROW_RADIUS, 0);
    lv_obj_set_style_pad_hor(row, LIST_ROW_PAD_H, 0);
    lv_obj_set_style_pad_ver(row, 0, 0);
    lv_obj_set_style_pad_column(row, LIST_ROW_COL_GAP, 0);
    lv_obj_set_style_bg_grad_dir(row, LV_GRAD_DIR_NONE, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *slot = lv_obj_create(row);
    lv_obj_remove_flag(slot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(slot, LIST_SLOT_W, LIST_SLOT_W);
    lv_obj_set_style_bg_opa(slot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(slot, 0, 0);
    lv_obj_set_style_pad_all(slot, 0, 0);

    lv_obj_t *dot = lv_obj_create(slot);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(dot, LIST_DOT_SIZE, LIST_DOT_SIZE);
    lv_obj_center(dot);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_bg_color(
        dot, node_online ? lv_color_hex(UI_COL_SUCCESS) : current_theme.text_secondary, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    if (node_online) {
      lv_obj_set_style_shadow_color(dot, lv_color_hex(UI_COL_SUCCESS), 0);
      lv_obj_set_style_shadow_width(dot, LIST_DOT_GLOW_W, 0);
      lv_obj_set_style_shadow_opa(dot, LV_OPA_50, 0);
    }

    lv_obj_t *nm = lv_label_create(row);
    lv_obj_set_flex_grow(nm, 1);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(nm, s_nodes[i].name);
    lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, 0);
    s_list_name[i] = nm;

    char val[16];
    fmt_rssi(s_nodes[i].rssi, val, sizeof(val));
    lv_obj_t *rssi = lv_label_create(row);
    lv_label_set_text(rssi, val);
    lv_obj_set_style_text_font(rssi, &lv_font_montserrat_12, 0);
    s_list_val[i] = rssi;

    s_node_rows[i] = row;
  }

  node_select(s_node);
}

static void build_nodes_view(void) {
  lv_color_t accent = ui_theme_get_accent();
  ui_chrome_header(s_screen, "s_nodes", "/assets/icons/hub.bin");

  refresh_nodes();

  if (s_node < 0)
    s_node = 0;

  s_nodes_ok_fired = false;
  s_nodes_ok_active = false;

  if (s_nodes_list) {
    build_nodes_list();
    ui_chrome_footer(s_screen, LV_SYMBOL_UP LV_SYMBOL_DOWN " hop  OK chat  HOLD map");
    return;
  }

  int online = 0;
  for (int i = 0; i < s_rnode_count; i++)
    if (s_nodes[i].strong)
      online++;
  lv_obj_t *cap = lv_label_create(s_screen);
  lv_label_set_text_fmt(cap, "%d / %d s_nodes ONLINE", online, s_rnode_count);
  lv_obj_set_style_text_color(cap, current_theme.text_secondary, 0);
  lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
  lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, MESH_CAP_Y);

  for (int i = 0; i < s_rnode_count; i++) {
    lv_color_t col;
    int w;
    bool dashed;
    link_style(i, &col, &w, &dashed);
    s_link_pts[i][0].x = MAP_CENTER_X;
    s_link_pts[i][0].y = MAP_CENTER_Y;
    s_link_pts[i][1].x = NODE_POS[i].x;
    s_link_pts[i][1].y = NODE_POS[i].y;
    lv_obj_t *ln = lv_line_create(s_screen);
    lv_obj_align(ln, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_line_set_points(ln, s_link_pts[i], 2);
    lv_obj_set_style_line_width(ln, w, 0);
    lv_obj_set_style_line_color(ln, col, 0);
    lv_obj_set_style_line_opa(ln, dashed ? LV_OPA_40 : LV_OPA_COVER, 0);
    lv_obj_set_style_line_rounded(ln, true, 0);
  }

  for (int i = 0; i < s_rnode_count; i++) {
    lv_obj_t *wrap = lv_obj_create(s_screen);
    lv_obj_remove_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wrap, 0, 0);
    lv_obj_set_style_pad_all(wrap, 0, 0);
    lv_obj_set_style_pad_row(wrap, 1, 0);
    lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(
        wrap, LV_ALIGN_CENTER, NODE_POS[i].x - LCD_H_RES / 2, NODE_POS[i].y - LCD_V_RES / 2);

    s_node_pins[i] = make_badge(wrap, "/assets/icons/settings_input_antenna.bin", accent);

    lv_obj_t *nm = lv_label_create(wrap);
    lv_label_set_text(nm, s_nodes[i].name);
    lv_obj_set_style_text_font(nm, &lv_font_montserrat_12, 0);
    s_node_names[i] = nm;

    node_style_pin(i, i == s_node);
  }

  lv_obj_t *you_wrap = lv_obj_create(s_screen);
  lv_obj_remove_flag(you_wrap, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(you_wrap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(you_wrap, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(you_wrap, 0, 0);
  lv_obj_set_style_pad_all(you_wrap, 0, 0);
  lv_obj_set_style_pad_row(you_wrap, 1, 0);
  lv_obj_set_flex_flow(you_wrap, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(you_wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_align(
      you_wrap, LV_ALIGN_CENTER, MAP_CENTER_X - LCD_H_RES / 2, MAP_CENTER_Y - LCD_V_RES / 2);

  lv_obj_t *you_pin = make_badge(you_wrap, "/assets/icons/hub.bin", accent);
  lv_obj_set_size(you_pin, YOU_PIN_SZ, YOU_PIN_SZ);
  lv_obj_set_style_bg_color(you_pin, accent, 0);
  lv_obj_set_style_border_width(you_pin, 2, 0);

  lv_obj_t *you_lbl = lv_label_create(you_wrap);
  lv_label_set_text(you_lbl, "YOU");
  lv_obj_set_style_text_color(you_lbl, current_theme.text_main, 0);
  lv_obj_set_style_text_font(you_lbl, &lv_font_montserrat_12, 0);

  lv_obj_t *info = lit_panel(s_screen, MESH_INFO_W, MESH_INFO_H, accent);
  lv_obj_align(info, LV_ALIGN_BOTTOM_MID, 0, -MESH_INFO_BOT);
  lv_obj_set_style_pad_all(info, 6, 0);

  s_info_name = lv_label_create(info);
  lv_obj_set_style_text_font(s_info_name, &lv_font_montserrat_12, 0);
  lv_obj_align(s_info_name, LV_ALIGN_TOP_LEFT, 0, 0);

  s_info_meta = lv_label_create(info);
  lv_obj_set_style_text_font(s_info_meta, &lv_font_montserrat_12, 0);
  lv_obj_align(s_info_meta, LV_ALIGN_TOP_RIGHT, 0, 0);

  s_info_bar = lv_bar_create(info);
  lv_obj_set_size(s_info_bar, MESH_INFO_W - 20, 5);
  lv_obj_align(s_info_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_bar_set_range(s_info_bar, 0, 100);
  lv_obj_set_style_bg_color(s_info_bar, current_theme.bg_primary, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_info_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(s_info_bar, 2, LV_PART_MAIN);
  lv_obj_set_style_radius(s_info_bar, 2, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_info_bar, LV_OPA_COVER, LV_PART_INDICATOR);

  if (s_rnode_count == 0) {
    if (s_info_name != NULL)
      lv_label_set_text(s_info_name, "Listening for nodes...");
    if (s_info_meta != NULL)
      lv_label_set_text(s_info_meta, "");
  } else {
    node_update_info(s_node);
  }

  ui_chrome_footer(s_screen, LV_SYMBOL_UP LV_SYMBOL_DOWN " hop  OK chat  HOLD list");
}

static void build_configs_view(void) {
  s_menu = menu_component_create(s_screen, "CONFIGS", "/assets/icons/tune.bin");

  if (lora_session_active() != LORA_PROTO_MESHTASTIC) {
    menu_component_add_section(&s_menu, "RADIO PARAMETERS");
    menu_component_add_section(&s_menu, "Meshtastic only");
    menu_component_set_hint(&s_menu, "BACK home");
    return;
  }

  s_cfg_region = (int)mt_region_current();
  s_cfg_preset = (int)mt_preset_current();
  const mt_region_info_t *rg = mt_region_info((mt_region_t)s_cfg_region);
  const mt_preset_info_t *pr = mt_preset_info((mt_preset_t)s_cfg_preset);
  bool router = (mt_role_current() == MT_ROLE_ROUTER);

  menu_component_add_section(&s_menu, "RADIO (applies on reboot)");
  menu_component_add_selector(
      &s_menu, "/assets/icons/public.bin", "Region", (rg != NULL) ? rg->name : "?");
  menu_component_add_selector(
      &s_menu, "/assets/icons/speed.bin", "Preset", (pr != NULL) ? pr->name : "?");
  menu_component_add_toggle(&s_menu, "/assets/icons/router.bin", "Router mode", router);
  menu_component_set_hint(&s_menu,
                          LV_SYMBOL_LEFT LV_SYMBOL_RIGHT "  change   OK toggle   BACK home");
}

static lv_obj_t *make_companion_card(lv_obj_t *parent, bool linked, lv_color_t accent) {
  lv_color_t edge = linked ? lv_color_hex(UI_COL_SUCCESS) : accent;
  lv_obj_t *card = lit_panel(parent, 200, 60, edge);
  lv_obj_set_style_pad_all(card, 8, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(card, 8, 0);

  make_badge(card, "/assets/icons/bluetooth.bin", edge);

  lv_obj_t *txt = lv_obj_create(card);
  lv_obj_remove_flag(txt, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(txt, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(txt, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(txt, 0, 0);
  lv_obj_set_style_pad_all(txt, 0, 0);
  lv_obj_set_flex_flow(txt, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_grow(txt, 1);

  lv_obj_t *nm = lv_label_create(txt);
  lv_label_set_text(nm, "HighBoy Companion");
  lv_obj_set_style_text_color(nm, current_theme.text_main, 0);
  lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, 0);

  lv_obj_t *sub = lv_label_create(txt);
  lv_label_set_text(sub, linked ? "Linked  \xC2\xB7  BLE" : "v1.2  \xC2\xB7  BLE");
  lv_obj_set_style_text_color(sub, linked ? lv_color_hex(UI_COL_SUCCESS) : current_theme.text_secondary, 0);
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);

  if (linked) {
    lv_obj_t *chk = lv_label_create(card);
    lv_label_set_text(chk, LV_SYMBOL_OK);
    lv_obj_set_style_text_color(chk, lv_color_hex(UI_COL_SUCCESS), 0);
    lv_obj_set_style_text_font(chk, &lv_font_montserrat_14, 0);
  }
  return card;
}

static void conn_dots_cb(void *var, int32_t v) {
  lv_obj_t *label = (lv_obj_t *)var;
  static const char *DOTS[] = {"", ".", "..", "..."};
  int n = (int)v;
  if (n < 0)
    n = 0;
  if (n > DOTS_MAX)
    n = DOTS_MAX;
  char buf[40];
  snprintf(buf, sizeof(buf), "Waiting for app%s", DOTS[n]);
  lv_label_set_text(label, buf);
}

static void connect_poll_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen || s_view != VIEW_CONNECT) {
    lv_timer_delete(t);
    s_connect_timer = NULL;
    return;
  }
  bool connected = lora_session_app_connected();
  if (connected != s_linked) {
    s_linked = connected;
    if (connected) {
      ui_feedback(UI_FB_EMULATE);
      notify(NOTIFY_LORA, "Companion linked");
    }
    build_screen();
  }
}

static void build_connect(void) {
  lv_color_t accent = ui_theme_get_accent();
  ui_chrome_header(s_screen, PROTOS[s_proto], "/assets/icons/bluetooth.bin");

  s_linked = lora_session_app_connected();

  if (!s_linked) {
    lora_session_app_connect();

    s_status_label = lv_label_create(s_screen);
    lv_label_set_text(s_status_label, "Waiting for app");
    lv_obj_set_style_text_color(s_status_label, current_theme.text_main, 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, 52);

    lv_anim_t ad;
    lv_anim_init(&ad);
    lv_anim_set_var(&ad, s_status_label);
    lv_anim_set_exec_cb(&ad, conn_dots_cb);
    lv_anim_set_values(&ad, 0, DOTS_MAX);
    lv_anim_set_duration(&ad, DOTS_STEP_MS * DOTS_MAX);
    lv_anim_set_repeat_count(&ad, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&ad);

    waves_create(
        s_screen, LV_ALIGN_CENTER, 0, -18, LV_SYMBOL_BLUETOOTH, "/assets/icons/bluetooth.bin");

    lv_obj_t *card = make_companion_card(s_screen, false, accent);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 86);

    s_connect_timer = lv_timer_create(connect_poll_cb, CONNECT_POLL_MS, NULL);

    ui_chrome_footer(s_screen, "BACK to cancel");
  } else {
    lv_obj_t *st = lv_label_create(s_screen);
    lv_label_set_text(st, "Companion linked!");
    lv_obj_set_style_text_color(st, lv_color_hex(UI_COL_SUCCESS), 0);
    lv_obj_set_style_text_font(st, &lv_font_montserrat_14, 0);
    lv_obj_align(st, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_fade_in(st, ENTRY_MS, 0);

    lv_obj_t *card = make_companion_card(s_screen, true, accent);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 6);
    lv_obj_fade_in(card, ENTRY_MS, 0);
    card_rise(card);

    ui_chrome_footer(s_screen, "BACK = Exit");
  }
}

static void add_bubble(bool outgoing, const char *who, const char *text) {
  if (s_chat_list == NULL)
    return;
  lv_color_t accent = ui_theme_get_accent();

  lv_obj_t *row = lv_obj_create(s_chat_list);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_height(row, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row,
                        outgoing ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);

  lv_obj_t *group = lv_obj_create(row);
  lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(group, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(group, 0, 0);
  lv_obj_set_style_pad_all(group, 2, 0);
  lv_obj_set_style_pad_row(group, 2, 0);
  lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(group,
                        LV_FLEX_ALIGN_START,
                        outgoing ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START);

  lv_obj_t *bubble = lv_obj_create(group);
  lv_obj_remove_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(bubble, BUBBLE_MAX_W, 0);
  lv_obj_set_style_pad_all(bubble, 7, 0);
  lv_obj_set_style_radius(bubble, 9, 0);
  lv_obj_set_style_bg_opa(bubble, outgoing ? LV_OPA_20 : LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(bubble, outgoing ? accent : current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_grad_dir(bubble, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(bubble, 1, 0);
  lv_obj_set_style_border_color(bubble, outgoing ? accent : current_theme.border_inactive, 0);
  lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(bubble, 2, 0);

  if (!outgoing && who != NULL) {
    lv_obj_t *nm = lv_label_create(bubble);
    char sender[48];
    snprintf(sender, sizeof(sender), "> %s", who);
    lv_label_set_text(nm, sender);
    lv_obj_set_style_text_color(nm, accent, 0);
    lv_obj_set_style_text_font(nm, &lv_font_montserrat_12, 0);
  }

  lv_obj_t *body = lv_label_create(bubble);
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_max_width(body, BUBBLE_MAX_W - 16, 0);
  lv_label_set_text(body, text);
  lv_obj_set_style_text_color(body, current_theme.text_main, 0);
  lv_obj_set_style_text_font(body, &lv_font_montserrat_12, 0);

  lv_obj_scroll_to_view(bubble, LV_ANIM_ON);
}

static void chat_drain(void) {
  if (s_chat_list == NULL)
    return;
  lora_msg_t batch[CHAT_BATCH];
  uint16_t got;
  while ((got = lora_session_msg_since(&s_chat_seq, batch, CHAT_BATCH)) > 0) {
    for (uint16_t i = 0; i < got; i++)
      add_bubble(batch[i].outgoing, batch[i].outgoing ? NULL : batch[i].who, batch[i].text);
  }
}

static void chat_poll_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen || s_view != VIEW_CHAT) {
    lv_timer_delete(t);
    s_chat_poll = NULL;
    return;
  }
  chat_drain();
}

static void on_kb_submit(const char *text, void *user_data) {
  (void)user_data;
  if (text == NULL || text[0] == '\0' || s_view != VIEW_CHAT)
    return;
  esp_err_t err = lora_session_send_text(text);
  if (err == ESP_OK) {
    ui_feedback(UI_FB_WRITE);
    chat_drain();
  } else {
    notify(NOTIFY_LORA, "Send failed");
  }
}

static void build_chat(void) {
  const char *title =
      (lora_session_active() == LORA_PROTO_MESHTASTIC) ? "MESH BROADCAST" : "PUBLIC CHANNEL";
  ui_chrome_header(s_screen, title, "/assets/icons/hub.bin");
  s_hint = ui_chrome_footer(s_screen, LV_SYMBOL_KEYBOARD " OK write   BACK nodes");

  s_chat_list = lv_obj_create(s_screen);
  lv_obj_set_size(s_chat_list, LCD_H_RES, LCD_V_RES - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(s_chat_list, LV_ALIGN_TOP_LEFT, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(s_chat_list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_chat_list, 0, 0);
  lv_obj_set_style_pad_all(s_chat_list, 6, 0);
  lv_obj_set_style_pad_row(s_chat_list, 6, 0);
  lv_obj_set_flex_flow(s_chat_list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(s_chat_list, LV_DIR_VER);
  lv_obj_remove_flag(s_chat_list, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_remove_flag(s_chat_list, LV_OBJ_FLAG_SCROLL_MOMENTUM);
  lv_obj_set_scrollbar_mode(s_chat_list, LV_SCROLLBAR_MODE_AUTO);

  s_chat_seq = 0;
  chat_drain();

  s_chat_poll = lv_timer_create(chat_poll_cb, CHAT_POLL_MS, NULL);
}

static void build_screen(void) {
  stop_timers();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_chat_list = NULL;
  s_status_label = NULL;
  s_hint = NULL;
  s_info_name = NULL;
  s_info_meta = NULL;
  s_info_bar = NULL;
  s_node_list = NULL;
  for (int i = 0; i < NODE_COUNT; i++) {
    s_node_pins[i] = NULL;
    s_node_names[i] = NULL;
    s_node_rows[i] = NULL;
    s_list_name[i] = NULL;
    s_list_val[i] = NULL;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  lv_obj_t *fade_target = NULL;

  if (s_view == VIEW_PROTO) {
    build_proto_view();
    fade_target = s_menu.items_cont;
  } else if (s_view == VIEW_HOME) {
    build_home_view();
    fade_target = s_menu.items_cont;
  } else if (s_view == VIEW_NODES) {
    build_nodes_view();
    fade_target = s_screen;
  } else if (s_view == VIEW_CONFIGS) {
    build_configs_view();
    fade_target = s_menu.items_cont;
  } else if (s_view == VIEW_CONNECT) {
    build_connect();
    fade_target = s_screen;
  } else {
    build_chat();
    fade_target = s_chat_list;
  }

  if (fade_target)
    lv_obj_fade_in(fade_target, ENTRY_MS, 0);

  ui_input_set_screen_handler(lora_chat_input, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

static void cycle_config(int sel, int dir) {
  if (sel == CFG_REGION) {
    int n = mt_region_count();
    if (n <= 0)
      return;
    s_cfg_region = (s_cfg_region + dir + n) % n;
    mt_region_set((mt_region_t)s_cfg_region);
    const mt_region_info_t *rg = mt_region_info((mt_region_t)s_cfg_region);
    menu_component_set_selector_value(&s_menu, sel, (rg != NULL) ? rg->name : "?");
  } else if (sel == CFG_PRESET) {
    int n = mt_preset_count();
    if (n <= 0)
      return;
    s_cfg_preset = (s_cfg_preset + dir + n) % n;
    mt_preset_set((mt_preset_t)s_cfg_preset);
    const mt_preset_info_t *pr = mt_preset_info((mt_preset_t)s_cfg_preset);
    menu_component_set_selector_value(&s_menu, sel, (pr != NULL) ? pr->name : "?");
  }
}

static void exit_to_menu(void) {
  ui_theme_set_protocol(PROTOCOL_NONE);
  ui_switch_screen(SCREEN_MENU);
}

static void lora_chat_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  switch (s_view) {
    case VIEW_PROTO:
      switch (ev->button) {
        case INPUT_BTN_DOWN:
          if (nav)
            menu_component_next(&s_menu);
          break;
        case INPUT_BTN_UP:
          if (nav)
            menu_component_prev(&s_menu);
          break;
        case INPUT_BTN_OK:
          if (press) {
            s_proto = menu_component_get_selected(&s_menu);
            ui_feedback(UI_FB_SELECT);
            lora_proto_t want = proto_for_sel(s_proto);
            lora_proto_t cur = lora_session_active();
            if (cur != LORA_PROTO_NONE && cur != want) {
              notify(NOTIFY_LORA, "Reboot to switch protocol");
              s_proto = sel_for_proto(cur);
            } else {
              start_active_proto(want);
            }
            s_view = VIEW_HOME;
            s_home_sel = 0;
            build_screen();
          }
          break;
        case INPUT_BTN_BACK:
          if (press)
            exit_to_menu();
          break;
        default:
          break;
      }
      break;

    case VIEW_HOME:
      switch (ev->button) {
        case INPUT_BTN_DOWN:
          if (nav)
            menu_component_next(&s_menu);
          break;
        case INPUT_BTN_UP:
          if (nav)
            menu_component_prev(&s_menu);
          break;
        case INPUT_BTN_OK:
          if (press) {
            s_home_sel = menu_component_get_selected(&s_menu);
            ui_feedback(UI_FB_SELECT);
            if (s_home_sel == 3) {
              ui_switch_screen(SCREEN_LORA_CHANNELS);
            } else if (s_home_sel == 4) {
              ui_switch_screen(SCREEN_LORA_POSITION);
            } else if (s_home_sel == 5) {
              ui_switch_screen(SCREEN_LORA_TELEMETRY);
            } else if (s_home_sel == 6) {
              ui_switch_screen(SCREEN_LORA_SECURE_DM);
            } else if (s_home_sel == 7) {
              ui_switch_screen(SCREEN_LORA_TRACEROUTE);
            } else {
              s_view = (s_home_sel == 0)   ? VIEW_CONNECT
                       : (s_home_sel == 1) ? VIEW_NODES
                                           : VIEW_CONFIGS;
              build_screen();
            }
          }
          break;
        case INPUT_BTN_BACK:
          if (press) {
            s_view = VIEW_PROTO;
            build_screen();
          }
          break;
        default:
          break;
      }
      break;

    case VIEW_CONNECT:
      if (ev->button == INPUT_BTN_BACK && press) {
        s_view = VIEW_HOME;
        build_screen();
      }
      break;

    case VIEW_NODES:
      switch (ev->button) {
        case INPUT_BTN_DOWN:
          if (nav && s_rnode_count > 0) {
            s_node = (s_node + 1) % s_rnode_count;
            node_select(s_node);
          }
          break;
        case INPUT_BTN_UP:
          if (nav && s_rnode_count > 0) {
            s_node = (s_node - 1 + s_rnode_count) % s_rnode_count;
            node_select(s_node);
          }
          break;
        case INPUT_BTN_OK:
          if (press) {
            s_nodes_ok_active = true;
            s_nodes_ok_fired = false;
          } else if (ev->action == INPUT_ACTION_LONG_PRESS) {
            if (s_nodes_ok_active && !s_nodes_ok_fired) {
              s_nodes_ok_fired = true;
              s_nodes_list = !s_nodes_list;
              ui_feedback(UI_FB_SELECT);
              build_screen();
            }
          } else if (ev->action == INPUT_ACTION_RELEASE) {
            bool short_press = s_nodes_ok_active && !s_nodes_ok_fired;
            s_nodes_ok_active = false;
            s_nodes_ok_fired = false;
            if (short_press) {
              ui_feedback(UI_FB_SELECT);
              s_view = VIEW_CHAT;
              build_screen();
            }
          }
          break;
        case INPUT_BTN_BACK:
          if (press) {
            s_view = VIEW_HOME;
            build_screen();
          }
          break;
        default:
          break;
      }
      break;

    case VIEW_CONFIGS: {
      int sel = menu_component_get_selected(&s_menu);
      switch (ev->button) {
        case INPUT_BTN_DOWN:
          if (nav)
            menu_component_next(&s_menu);
          break;
        case INPUT_BTN_UP:
          if (nav)
            menu_component_prev(&s_menu);
          break;
        case INPUT_BTN_LEFT:
          if (nav && lora_session_active() == LORA_PROTO_MESHTASTIC)
            cycle_config(sel, -1);
          break;
        case INPUT_BTN_RIGHT:
          if (nav && lora_session_active() == LORA_PROTO_MESHTASTIC)
            cycle_config(sel, +1);
          break;
        case INPUT_BTN_OK:
          if (press && sel == CFG_ROUTER) {
            bool now = (mt_role_current() == MT_ROLE_ROUTER);
            mt_role_set(now ? MT_ROLE_CLIENT : MT_ROLE_ROUTER);
            menu_component_toggle_item(&s_menu, sel);
          }
          break;
        case INPUT_BTN_BACK:
          if (press) {
            s_view = VIEW_HOME;
            build_screen();
          }
          break;
        default:
          break;
      }
      break;
    }

    case VIEW_CHAT:
      switch (ev->button) {
        case INPUT_BTN_DOWN:
          if (nav && s_chat_list) {
            int32_t sb = lv_obj_get_scroll_bottom(s_chat_list);
            if (sb > 0)
              lv_obj_scroll_by(s_chat_list, 0, -(sb < 36 ? sb : 36), LV_ANIM_OFF);
          }
          break;
        case INPUT_BTN_UP:
          if (nav && s_chat_list) {
            int32_t st = lv_obj_get_scroll_top(s_chat_list);
            if (st > 0)
              lv_obj_scroll_by(s_chat_list, 0, (st < 36 ? st : 36), LV_ANIM_OFF);
          }
          break;
        case INPUT_BTN_OK:
          if (press)
            keyboard_open(NULL, on_kb_submit, NULL);
          break;
        case INPUT_BTN_BACK:
          if (press) {
            s_view = VIEW_NODES;
            build_screen();
          }
          break;
        default:
          break;
      }
      break;

    default:
      break;
  }
}

void ui_lora_chat_open(void) {
  ui_theme_set_protocol(PROTOCOL_LORA);
  lora_proto_t cur = lora_session_active();
  s_view = VIEW_PROTO;
  s_proto = (cur != LORA_PROTO_NONE) ? sel_for_proto(cur) : 0;
  s_home_sel = 0;
  s_node = 0;
  s_rnode_count = 0;
  s_nodes_list = false;
  s_linked = false;
  s_chat_seq = 0;
  s_connect_timer = NULL;
  s_chat_poll = NULL;
  build_screen();
  ESP_LOGI(TAG, "LoRa mesh opened");
}

void ui_lora_chat_open_chat(void) {
  ui_theme_set_protocol(PROTOCOL_LORA);
  lora_proto_t cur = lora_session_active();
  s_view = VIEW_CHAT;
  s_proto = (cur != LORA_PROTO_NONE) ? sel_for_proto(cur) : 0;
  s_home_sel = 0;
  s_node = 0;
  s_rnode_count = 0;
  s_nodes_list = false;
  s_linked = false;
  s_chat_seq = 0;
  s_connect_timer = NULL;
  s_chat_poll = NULL;
  build_screen();
  ESP_LOGI(TAG, "LoRa chat opened");
}
