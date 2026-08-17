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

#include "nfc_ui_common.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "audio_i2s.h"
#include "drv2605l.h"
#include "ui_theme.h"

#define RING_MIN    26
#define RING_MAX    116
#define RING_PERIOD 1500

#define NFC_SND_AMP 0.40f

#define NFC_SND_TASK_STACK_SIZE      8192
#define NFC_SND_TASK_PRIORITY        SYS_PRIO_SERVICE_LO
#define DRV2605L_EFFECT_DOUBLE_CLICK 10

static const audio_note_t SND_FOUND_NOTES[] = {
    {1318, 70},
    {1976, 120},
};
static const audio_note_t SND_SAVE_NOTES[] = {
    {1568, 45},
    {2093, 80},
};

static volatile bool s_snd_busy = false;

static void nfc_snd_task(void *arg) {
  nfc_ui_sound_t kind = (nfc_ui_sound_t)(intptr_t)arg;
  if (kind == NFC_SND_SAVE) {
    (void)drv2605l_play_effect(DRV2605L_EFFECT_DOUBLE_CLICK);
    audio_i2s_play_song(SND_SAVE_NOTES, 2, NFC_SND_AMP);
  } else {
    (void)drv2605l_play_effect(1);
    audio_i2s_play_song(SND_FOUND_NOTES, 2, NFC_SND_AMP);
  }
  s_snd_busy = false;
  vTaskDelete(NULL);
}

void nfc_ui_play_sound(nfc_ui_sound_t kind) {
  if (s_snd_busy)
    return;
  s_snd_busy = true;
  if (xTaskCreatePinnedToCore(nfc_snd_task,
                              "nfc_snd",
                              NFC_SND_TASK_STACK_SIZE,
                              (void *)(intptr_t)kind,
                              NFC_SND_TASK_PRIORITY,
                              NULL,
                              SYS_CORE_UI) != pdPASS)
    s_snd_busy = false;
}

lv_obj_t *nfc_ui_header(lv_obj_t *parent, const char *title) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, title);
  lv_obj_set_style_text_color(lbl, ui_theme_get_accent(), 0);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 10);

  lv_obj_t *rule = lv_obj_create(parent);
  lv_obj_remove_flag(rule, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(rule, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(rule, lv_pct(70), 2);
  lv_obj_align(rule, LV_ALIGN_TOP_MID, 0, 32);
  lv_obj_set_style_border_width(rule, 0, 0);
  lv_obj_set_style_radius(rule, 1, 0);
  lv_obj_set_style_bg_color(rule, ui_theme_get_accent(), 0);
  lv_obj_set_style_bg_opa(rule, LV_OPA_40, 0);
  return lbl;
}

typedef struct {
  uint32_t top, bot, edge;
  bool light;
  bool stripe;
} card_style_t;
static const card_style_t STYLES[] = {
    {0x3A1170, 0x140230, 0xB060FF, false, false},
    {0xEDEDF2, 0xCFCFD6, 0x6B3FA0, true, false},
    {0x123A78, 0x05122E, 0x4D9BFF, false, false},
    {0x0E5A4A, 0x06241E, 0x37E0A8, false, true},
    {0x6E1430, 0x250410, 0xFF5C7A, false, false},
    {0x6E4A12, 0x281806, 0xFFC23D, false, true},
    {0xE7E2D6, 0xCEC7B6, 0x8A6A22, true, false},
    {0x20242E, 0x0A0C12, 0x9AA6C2, false, false},
};
#define N_STYLES ((int)(sizeof(STYLES) / sizeof(STYLES[0])))

static const card_style_t *card_style(const nfc_sim_card_t *c) {
  if (c == NULL)
    return &STYLES[0];
  uint32_t h = 2166136261u;
  for (int i = 0; i < c->uid_len; i++)
    h = (h ^ c->uid[i]) * 16777619u;
  return &STYLES[h % (uint32_t)N_STYLES];
}

lv_color_t nfc_ui_card_color(const nfc_sim_card_t *card) {
  return lv_color_hex(card_style(card)->edge);
}

static const char *card_details(const char *type) {
  if (strstr(type, "1K"))
    return "1 KB  16 sectors";
  if (strstr(type, "4K"))
    return "4 KB  40 sectors";
  if (strstr(type, "NTAG215"))
    return "504 B  NDEF";
  if (strstr(type, "Ultralight"))
    return "64 B  NDEF";
  if (strstr(type, "DESFire"))
    return "AES  ISO14443-4";
  return "ISO14443-A";
}

lv_obj_t *nfc_ui_card_panel(lv_obj_t *parent, const nfc_sim_card_t *card) {
  const card_style_t *st = card_style(card);
  lv_color_t text = st->light ? lv_color_hex(0x1A1A22) : lv_color_white();
  lv_color_t edge = lv_color_hex(st->edge);

  lv_obj_t *panel = lv_obj_create(parent);
  lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(panel, 210, 122);
  lv_obj_set_style_radius(panel, 14, 0);
  lv_obj_set_style_pad_all(panel, 12, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(st->top), 0);
  lv_obj_set_style_bg_grad_color(panel, lv_color_hex(st->bot), 0);
  lv_obj_set_style_bg_grad_dir(panel, LV_GRAD_DIR_VER, 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_border_color(panel, edge, 0);
  lv_obj_set_style_shadow_color(panel, edge, 0);
  lv_obj_set_style_shadow_width(panel, 10, 0);
  lv_obj_set_style_shadow_opa(panel, LV_OPA_30, 0);

  lv_obj_t *chip = lv_obj_create(panel);
  lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(chip, 30, 22);
  lv_obj_align(chip, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_radius(chip, 5, 0);
  lv_obj_set_style_border_width(chip, 0, 0);
  lv_obj_set_style_bg_color(chip, lv_color_hex(0xD9A521), 0);
  lv_obj_set_style_bg_grad_color(chip, lv_color_hex(0xF4D36B), 0);
  lv_obj_set_style_bg_grad_dir(chip, LV_GRAD_DIR_VER, 0);
  for (int i = 0; i < 2; i++) {
    lv_obj_t *ln = lv_obj_create(chip);
    lv_obj_remove_flag(ln, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ln, 26, 1);
    lv_obj_align(ln, LV_ALIGN_CENTER, 0, i == 0 ? -5 : 5);
    lv_obj_set_style_border_width(ln, 0, 0);
    lv_obj_set_style_radius(ln, 0, 0);
    lv_obj_set_style_bg_color(ln, lv_color_hex(0x7A5A10), 0);
    lv_obj_set_style_bg_opa(ln, LV_OPA_70, 0);
  }

  if (st->stripe) {
    lv_obj_t *line = lv_obj_create(panel);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(line, lv_pct(100), 3);
    lv_obj_align(line, LV_ALIGN_TOP_LEFT, 0, 26);
    lv_obj_set_style_radius(line, 2, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_bg_color(line, edge, 0);
  }

  if (card == NULL)
    return panel;

  bool has_name = (card->name[0] != '\0');
  int uid_y = has_name ? 66 : 54;
  int meta_y = has_name ? 83 : 76;

  lv_obj_t *title = lv_label_create(panel);
  lv_obj_set_width(title, lv_pct(100));
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_label_set_text(title, has_name ? card->name : card->type);
  lv_obj_set_style_text_color(title, text, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 30);

  if (has_name) {
    lv_obj_t *sub = lv_label_create(panel);
    lv_obj_set_width(sub, lv_pct(100));
    lv_label_set_long_mode(sub, LV_LABEL_LONG_DOT);
    lv_label_set_text(sub, card->type);
    lv_obj_set_style_text_color(sub, text, 0);
    lv_obj_set_style_text_opa(sub, LV_OPA_60, 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
    lv_obj_align(sub, LV_ALIGN_TOP_LEFT, 0, 49);
  }

  char uid[24];
  nfc_sim_format_uid(card, uid, sizeof(uid));
  lv_obj_t *uidl = lv_label_create(panel);
  lv_obj_set_width(uidl, lv_pct(100));
  lv_label_set_long_mode(uidl, LV_LABEL_LONG_DOT);
  lv_label_set_text_fmt(uidl, "UID  %s", uid);
  lv_obj_set_style_text_color(uidl, edge, 0);
  lv_obj_set_style_text_font(uidl, &lv_font_montserrat_12, 0);
  lv_obj_align(uidl, LV_ALIGN_TOP_LEFT, 0, uid_y);

  lv_obj_t *meta = lv_label_create(panel);
  lv_obj_set_width(meta, lv_pct(100));
  lv_label_set_long_mode(meta, LV_LABEL_LONG_DOT);
  lv_label_set_text_fmt(meta, "%s   SAK 0x%02X", card_details(card->type), card->sak);
  lv_obj_set_style_text_color(meta, text, 0);
  lv_obj_set_style_text_opa(meta, LV_OPA_50, 0);
  lv_obj_set_style_text_font(meta, &lv_font_montserrat_12, 0);
  lv_obj_align(meta, LV_ALIGN_TOP_LEFT, 0, meta_y);

  return panel;
}

void nfc_ui_field_create(nfc_ui_field_t *f, lv_obj_t *parent, lv_color_t color) {
  for (int i = 0; i < 3; i++) {
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 3, 0);
    lv_obj_set_style_border_color(r, color, 0);
    lv_obj_set_size(r, RING_MIN, RING_MIN);
    lv_obj_align(r, LV_ALIGN_CENTER, 0, 0);
    f->ring[i] = r;
  }
}

void nfc_ui_field_tick(nfc_ui_field_t *f, uint32_t elapsed_ms) {
  for (int i = 0; i < 3; i++) {
    if (f->ring[i] == NULL)
      continue;
    uint32_t ph = (elapsed_ms + (uint32_t)i * (RING_PERIOD / 3)) % RING_PERIOD;
    float t = (float)ph / (float)RING_PERIOD;
    int size = RING_MIN + (int)(t * (RING_MAX - RING_MIN));
    lv_opa_t opa = (lv_opa_t)((1.0f - t) * 255.0f);
    lv_obj_set_size(f->ring[i], size, size);
    lv_obj_align(f->ring[i], LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_border_opa(f->ring[i], opa, 0);
  }
}
