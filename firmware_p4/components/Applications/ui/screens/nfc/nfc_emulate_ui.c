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

#include "nfc_emulate_ui.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "lvgl.h"

#include "st7789.h"

#include "assets_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "highboy_nfc.h"
#include "keyboard_ui.h"
#include "mf_classic_emu.h"
#include "msgbox_ui.h"
#include "nfc_listener.h"
#include "nfc_sim.h"
#include "nfc_ui_common.h"
#include "notify_ui.h"
#include "page_dots_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_semantic.h"
#include "ui_theme.h"

#define TICK_MS     33
#define CARD_ICON   "/assets/icons/contactless.bin"
#define TX_DOTS     3
#define CARD_Y_OFS  (-24)
#define FIELD_BOX_W 210
#define FIELD_BOX_H 122

#define MAX_CARDS      10
#define WALLET_FRONT_Y (-24)
#define WALLET_STEP    22
#define WALLET_TAP_Y   96
#define BACK1_W        196
#define BACK2_W        182
#define BACK_H         118
#define MFC1K_BLOCKS   64
#define MFC_BLOCK_SIZE 16
#define MFC_DUMP_DIR   "/sdcard/nfc/dumps"

enum { EM_NONE, EM_RUN };

static const char *TAG = "NFC_EMULATE_UI";

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_body = NULL;
static page_dots_t s_dots;
static int s_sel = 0;
static int s_count = 0;
static lv_timer_t *s_tick_timer = NULL;
static bool s_empty = false;

static lv_obj_t *s_ov = NULL;
static lv_obj_t *s_ov_status = NULL;
static nfc_ui_field_t s_ov_field;
static nfc_sim_card_t s_card;
EXT_RAM_BSS_ATTR static mfc_emu_card_data_t s_hw_card;
static bool s_hw_running = false;
static int s_em = EM_NONE;
static uint32_t s_em_start = 0;
static uint32_t s_status_tick = 0;

typedef struct {
  nfc_sim_card_t card;
  char path[128];
} nfc_emu_dump_entry_t;

EXT_RAM_BSS_ATTR static nfc_emu_dump_entry_t s_dumps[MAX_CARDS];

static esp_err_t load_mfc1k_dump(const char *path, mfc_emu_card_data_t *emu) {
  if (path == NULL || emu == NULL)
    return ESP_ERR_INVALID_ARG;

  FILE *fp = fopen(path, "rb");
  if (fp == NULL)
    return ESP_ERR_NOT_FOUND;

  memset(emu, 0, sizeof(*emu));
  size_t blocks = fread(emu->blocks, MFC_BLOCK_SIZE, MFC1K_BLOCKS, fp);
  int extra = fgetc(fp);
  fclose(fp);
  if (blocks != MFC1K_BLOCKS || extra != EOF)
    return ESP_ERR_INVALID_SIZE;

  memcpy(emu->uid, emu->blocks[0], 4);
  emu->uid_len = 4;
  emu->atqa[0] = 0x04;
  emu->atqa[1] = 0x00;
  emu->sak = 0x08;
  emu->type = MF_CLASSIC_1K;
  emu->sector_count = 16;
  emu->total_blocks = MFC1K_BLOCKS;
  for (int sector = 0; sector < emu->sector_count; sector++) {
    const uint8_t *trailer = emu->blocks[(sector * 4) + 3];
    memcpy(emu->keys[sector].key_a, trailer, 6);
    memcpy(emu->keys[sector].key_b, trailer + 10, 6);
    emu->keys[sector].key_a_known = true;
    emu->keys[sector].key_b_known = true;
  }
  return ESP_OK;
}

static bool inspect_mfc1k_dump(const char *path, nfc_sim_card_t *card) {
  FILE *fp = fopen(path, "rb");
  if (fp == NULL)
    return false;

  uint8_t block0[MFC_BLOCK_SIZE];
  size_t got = fread(block0, 1, sizeof(block0), fp);
  bool valid = got == sizeof(block0) && fseek(fp, 0, SEEK_END) == 0 &&
               ftell(fp) == MFC1K_BLOCKS * MFC_BLOCK_SIZE;
  fclose(fp);
  if (!valid)
    return false;

  memset(card, 0, sizeof(*card));
  memcpy(card->uid, block0, 4);
  card->uid_len = 4;
  card->atqa = 0x0400;
  card->sak = 0x08;
  snprintf(card->name,
           sizeof(card->name),
           "MFC %02X%02X%02X%02X",
           card->uid[0],
           card->uid[1],
           card->uid[2],
           card->uid[3]);
  snprintf(card->type, sizeof(card->type), "MIFARE Classic 1K");
  return true;
}

static int scan_real_dumps(void) {
  memset(s_dumps, 0, sizeof(s_dumps));
  DIR *dir = opendir(MFC_DUMP_DIR);
  if (dir == NULL) {
    ESP_LOGW(TAG, "Dump directory not available: %s", MFC_DUMP_DIR);
    return 0;
  }

  int count = 0;
  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL && count < MAX_CARDS) {
    size_t len = strlen(ent->d_name);
    if (len < 8 || strncmp(ent->d_name, "mf_", 3) != 0 ||
        strcmp(ent->d_name + len - 4, ".bin") != 0)
      continue;

    int n =
        snprintf(s_dumps[count].path, sizeof(s_dumps[count].path), MFC_DUMP_DIR "/%s", ent->d_name);
    if (n < 0 || (size_t)n >= sizeof(s_dumps[count].path))
      continue;
    if (!inspect_mfc1k_dump(s_dumps[count].path, &s_dumps[count].card))
      continue;

    ESP_LOGI(TAG,
             "Found real dump %s UID=%02X%02X%02X%02X",
             s_dumps[count].path,
             s_dumps[count].card.uid[0],
             s_dumps[count].card.uid[1],
             s_dumps[count].card.uid[2],
             s_dumps[count].card.uid[3]);
    count++;
  }
  closedir(dir);
  return count;
}

static hb_nfc_err_t hw_emulation_start(const nfc_emu_dump_entry_t *dump) {
  if (dump == NULL)
    return HB_NFC_ERR_PARAM;

  esp_err_t err = load_mfc1k_dump(dump->path, &s_hw_card);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Cannot load emulation dump: %s (%s)", esp_err_to_name(err), dump->path);
    return HB_NFC_ERR_PARAM;
  }
  if (memcmp(dump->card.uid, s_hw_card.uid, 4) != 0) {
    ESP_LOGE(TAG, "Dump UID changed while opening %s", dump->path);
    return HB_NFC_ERR_PARAM;
  }

  highboy_nfc_config_t cfg = HIGHBOY_NFC_CONFIG_DEFAULT();
  err = highboy_nfc_init(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "NFC init failed: %s", esp_err_to_name(err));
    return HB_NFC_ERR_INTERNAL;
  }

  hb_nfc_err_t emu_err = nfc_listener_start_emu(&s_hw_card);
  if (emu_err != HB_NFC_OK) {
    /* The first access immediately after UI/radio hand-over can observe the
     * ST25R3916
     * register bank asleep. One short retry is deterministic and
     * mirrors the successful lab
     * bring-up sequence. */
    vTaskDelay(pdMS_TO_TICKS(20));
    emu_err = nfc_listener_start_emu(&s_hw_card);
  }
  if (emu_err == HB_NFC_OK) {
    s_hw_running = true;
    ESP_LOGI(TAG, "Real MIFARE emulation started from %s", dump->path);
  }
  return emu_err;
}

static void hw_emulation_stop(void) {
  if (!s_hw_running)
    return;
  nfc_listener_stop();
  s_hw_running = false;
}

static void opa_cb(void *var, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

static void transy_cb(void *var, int32_t v) {
  lv_obj_set_style_translate_y((lv_obj_t *)var, v, 0);
}

static void blink_loop(lv_obj_t *o, uint32_t period, int32_t delay) {
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, opa_cb);
  lv_anim_set_values(&a, 255, 110);
  lv_anim_set_duration(&a, period / 2);
  lv_anim_set_playback_duration(&a, period / 2);
  lv_anim_set_delay(&a, delay);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
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

static lv_obj_t *lit_panel(lv_obj_t *parent, int w, int h) {
  lv_obj_t *p = lv_obj_create(parent);
  lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(p, w, h);
  lv_obj_set_style_radius(p, 13, 0);
  lv_obj_set_style_bg_color(p, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_grad_dir(p, LV_GRAD_DIR_NONE, 0);
  lv_obj_set_style_border_width(p, 1, 0);
  lv_obj_set_style_border_color(p, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_color(p, current_theme.border_accent, 0);
  lv_obj_set_style_shadow_width(p, 16, 0);
  lv_obj_set_style_shadow_opa(p, LV_OPA_40, 0);
  lv_obj_set_style_shadow_spread(p, -4, 0);
  return p;
}

static void build_empty(const char *icon, const char *title, const char *sub) {
  lv_obj_t *card = lit_panel(s_screen, 200, 104);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 6);
  lv_obj_set_style_pad_all(card, 10, 0);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 6, 0);

  lv_image_dsc_t *dsc = assets_get(icon);
  if (dsc != NULL) {
    lv_obj_t *img = lv_image_create(card);
    lv_image_set_src(img, dsc);
    lv_obj_set_style_image_recolor(img, current_theme.text_main, 0);
    lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
  }

  lv_obj_t *t = lv_label_create(card);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(t, current_theme.text_main, 0);

  lv_obj_t *s = lv_label_create(card);
  lv_label_set_text(s, sub);
  lv_obj_set_style_text_font(s, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s, current_theme.text_secondary, 0);
}

static lv_obj_t *
wallet_back(lv_obj_t *parent, const nfc_sim_card_t *c, int w, int y_ofs, lv_opa_t opa) {
  lv_color_t edge = nfc_ui_card_color(c);
  lv_obj_t *b = lv_obj_create(parent);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, w, BACK_H);
  lv_obj_set_style_radius(b, 14, 0);
  lv_obj_set_style_bg_color(b, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, edge, 0);
  lv_obj_set_style_shadow_color(b, edge, 0);
  lv_obj_set_style_shadow_width(b, 12, 0);
  lv_obj_set_style_shadow_opa(b, LV_OPA_30, 0);
  lv_obj_set_style_shadow_spread(b, -4, 0);
  lv_obj_set_style_opa(b, opa, 0);
  lv_obj_align(b, LV_ALIGN_CENTER, 0, y_ofs);
  return b;
}

static void wallet_build(void) {
  if (s_body != NULL) {
    lv_obj_del(s_body);
    s_body = NULL;
  }

  s_body = lv_obj_create(s_screen);
  lv_obj_remove_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_body, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(s_body, lv_pct(100), ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H);
  lv_obj_align(s_body, LV_ALIGN_TOP_MID, 0, UI_CHROME_HEADER_H);
  lv_obj_set_style_bg_opa(s_body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s_body, 0, 0);
  lv_obj_set_style_pad_all(s_body, 0, 0);

  if (s_count >= 3)
    wallet_back(s_body,
                &s_dumps[(s_sel + 2) % s_count].card,
                BACK2_W,
                WALLET_FRONT_Y + 2 * WALLET_STEP,
                LV_OPA_40);
  if (s_count >= 2)
    wallet_back(s_body,
                &s_dumps[(s_sel + 1) % s_count].card,
                BACK1_W,
                WALLET_FRONT_Y + WALLET_STEP,
                LV_OPA_70);

  lv_obj_t *front = nfc_ui_card_panel(s_body, &s_dumps[s_sel].card);
  lv_obj_align(front, LV_ALIGN_CENTER, 0, WALLET_FRONT_Y);
  card_rise(front);

  lv_obj_t *tap = lv_obj_create(s_body);
  lv_obj_remove_flag(tap, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(tap, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(tap, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(tap, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(tap, 0, 0);
  lv_obj_set_style_pad_all(tap, 0, 0);
  lv_obj_set_style_pad_column(tap, 7, 0);
  lv_obj_set_flex_flow(tap, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(tap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  const int emu_body_h = ui_screen_h() - UI_CHROME_HEADER_H - UI_CHROME_FOOTER_H;
  lv_obj_align(tap, LV_ALIGN_CENTER, 0, LV_MIN(WALLET_TAP_Y, emu_body_h / 2 - 7));

  lv_obj_t *dot = lv_obj_create(tap);
  lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(dot, 8, 8);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(UI_COL_SUCCESS), 0);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  blink_loop(dot, 900, 0);

  lv_obj_t *txt = lv_label_create(tap);
  lv_label_set_text(txt, "tap to broadcast");
  lv_obj_set_style_text_font(txt, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(txt, lv_color_hex(UI_COL_SUCCESS), 0);

  s_dots = page_dots_create(s_body, s_count, LV_ALIGN_BOTTOM_MID, 0, -4);
  page_dots_set(&s_dots, s_sel);
}

static void overlay_close(void) {
  hw_emulation_stop();
  if (s_ov) {
    lv_obj_del(s_ov);
    s_ov = NULL;
  }
  s_ov_status = NULL;
  for (int i = 0; i < 3; i++)
    s_ov_field.ring[i] = NULL;
  s_em = EM_NONE;
}

static void overlay_start(const nfc_emu_dump_entry_t *dump) {
  hb_nfc_err_t err = hw_emulation_start(dump);
  if (err != HB_NFC_OK) {
    notify(NOTIFY_WARNING, "Could not start NFC emulation");
    return;
  }
  s_card = dump->card;

  s_ov = lv_obj_create(s_screen);
  lv_obj_set_size(s_ov, lv_pct(100), lv_pct(100));
  lv_obj_center(s_ov);
  lv_obj_remove_flag(s_ov, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(s_ov, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_ov, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(s_ov, 0, 0);
  lv_obj_set_style_pad_all(s_ov, 0, 0);

  ui_chrome_header_overlay(s_ov, "EMULATE", CARD_ICON);
  ui_chrome_footer(s_ov, "BACK  Stop");

  lv_obj_t *field_box = lv_obj_create(s_ov);
  lv_obj_remove_flag(field_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(field_box, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(field_box, FIELD_BOX_W, FIELD_BOX_H);
  lv_obj_align(field_box, LV_ALIGN_CENTER, 0, CARD_Y_OFS);
  lv_obj_set_style_bg_opa(field_box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(field_box, 0, 0);
  lv_obj_set_style_pad_all(field_box, 0, 0);

  nfc_ui_field_create(&s_ov_field, field_box, ui_theme_get_accent());

  lv_obj_t *card = nfc_ui_card_panel(s_ov, &s_card);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, CARD_Y_OFS);
  lv_obj_fade_in(card, 280, 0);
  card_rise(card);

  s_ov_status = lv_label_create(s_ov);
  lv_label_set_text(s_ov_status, "LISTENING");
  lv_obj_set_style_text_font(s_ov_status, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(s_ov_status, current_theme.border_accent, 0);
  lv_obj_align(s_ov_status, LV_ALIGN_BOTTOM_MID, 0, -46);
  blink_loop(s_ov_status, 900, 0);

  lv_obj_t *dots = lv_obj_create(s_ov);
  lv_obj_remove_flag(dots, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(dots, 44, 12);
  lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -30);
  lv_obj_set_style_bg_opa(dots, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(dots, 0, 0);
  lv_obj_set_style_pad_all(dots, 0, 0);
  lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(dots, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(dots, 6, 0);
  for (int i = 0; i < TX_DOTS; i++) {
    lv_obj_t *d = lv_obj_create(dots);
    lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(d, 6, 6);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(d, 0, 0);
    lv_obj_set_style_bg_color(d, current_theme.border_accent, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    blink_loop(d, 900, (int32_t)i * 300);
  }

  s_em = EM_RUN;
  s_em_start = lv_tick_get();
  s_status_tick = 0;
  ui_feedback(UI_FB_EMULATE);
}

static void field_tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_tick_timer = NULL;
    return;
  }
  if (s_em == EM_RUN) {
    nfc_ui_field_tick(&s_ov_field, lv_tick_get() - s_em_start);
    uint32_t now = lv_tick_get();
    if (s_ov_status != NULL && now - s_status_tick >= 250) {
      mfc_emu_stats_t stats = mfc_emu_get_stats();
      char line[64];
      snprintf(line,
               sizeof(line),
               "%s  A:%d  R:%d",
               mfc_emu_state_str(mfc_emu_get_state()),
               stats.successful_auths,
               stats.reads_served);
      lv_label_set_text(s_ov_status, line);
      s_status_tick = now;
    }
  }
}

static void nfc_emulate_input(const input_event_t *ev, void *ctx) {
  (void)ctx;
  const bool press = (ev->action == INPUT_ACTION_PRESS);
  const bool nav = press || (ev->action == INPUT_ACTION_REPEAT);

  if (s_em == EM_RUN) {
    if (ev->button == INPUT_BTN_BACK && press)
      overlay_close();
    return;
  }

  switch (ev->button) {
    case INPUT_BTN_BACK:
    case INPUT_BTN_LEFT:
      if (press)
        ui_switch_screen(SCREEN_NFC_MENU);
      break;
    case INPUT_BTN_DOWN:
      if (nav && !s_empty) {
        s_sel = (s_sel + 1) % s_count;
        wallet_build();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_UP:
      if (nav && !s_empty) {
        s_sel = (s_sel == 0) ? s_count - 1 : s_sel - 1;
        wallet_build();
        ui_feedback(UI_FB_NAV);
      }
      break;
    case INPUT_BTN_OK:
    case INPUT_BTN_RIGHT:
      if (press && !s_empty) {
        overlay_start(&s_dumps[s_sel]);
      }
      break;
    default:
      break;
  }
}

void ui_nfc_emulate_open(void) {
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_ov = NULL;
  s_ov_status = NULL;
  s_body = NULL;
  s_sel = 0;
  s_count = 0;
  for (int i = 0; i < 3; i++)
    s_ov_field.ring[i] = NULL;
  s_em = EM_NONE;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  s_count = scan_real_dumps();
  s_empty = (s_count == 0);

  if (s_empty) {
    ui_chrome_header(s_screen, "EMULATE", CARD_ICON);
    ui_chrome_footer(s_screen, "BACK  Back");
    build_empty(CARD_ICON, "No real dump found", "Read all 64 blocks first");
  } else {
    ui_chrome_header(s_screen, "EMULATE", CARD_ICON);
    ui_chrome_footer(s_screen, LV_SYMBOL_UP LV_SYMBOL_DOWN " Flip   OK Emulate   BACK Exit");
    s_sel = 0;
    wallet_build();
  }

  ui_input_set_screen_handler(nfc_emulate_input, NULL);
  if (s_tick_timer == NULL)
    s_tick_timer = lv_timer_create(field_tick_cb, TICK_MS, NULL);

  ui_screen_load_owned(&s_screen, s_screen);
}

void ui_nfc_emulate_stop(void) {
  hw_emulation_stop();
  s_em = EM_NONE;
}
