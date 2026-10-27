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

#include "lora_rnode_ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "lora_lab.h"
#include "notify_ui.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_semantic.h"
#include "ui_theme.h"

static const char *TAG = "LORA_RADIO_LAB";

#define LAB_REFRESH_MS 250
#define ENTRY_MS       180
#define CARD_W         216
#define CARD_RADIUS    12
#define BODY_TOP_Y     43
#define CARD_GAP       7
#define STATUS_H       68
#define RSSI_H         82
#define PACKET_H       79

#define ICON_ANTENNA "/assets/icons/settings_input_antenna.bin"

typedef struct {
  uint32_t hz;
  const char *label;
} lab_frequency_t;

static const lab_frequency_t LAB_FREQUENCIES[] = {
    {433920000U, "433.920 MHz"},
    {868100000U, "868.100 MHz"},
    {906875000U, "906.875 MHz"},
    {915000000U, "915.000 MHz"},
};
#define LAB_FREQ_COUNT   ((int)(sizeof(LAB_FREQUENCIES) / sizeof(LAB_FREQUENCIES[0])))
#define LAB_DEFAULT_FREQ 2

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_state_label = NULL;
static lv_obj_t *s_chip_label = NULL;
static lv_obj_t *s_rssi_label = NULL;
static lv_obj_t *s_rssi_stats_label = NULL;
static lv_obj_t *s_rssi_bar = NULL;
static lv_obj_t *s_packet_label = NULL;
static lv_obj_t *s_last_label = NULL;
static lv_timer_t *s_refresh_timer = NULL;
static int s_freq_index = LAB_DEFAULT_FREQ;

static lv_obj_t *lab_card(lv_obj_t *parent, int y, int h, lv_color_t accent) {
  lv_obj_t *card = lv_obj_create(parent);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, CARD_W, h);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y);
  lv_obj_set_style_radius(card, CARD_RADIUS, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, accent, 0);
  lv_obj_set_style_shadow_color(card, accent, 0);
  lv_obj_set_style_shadow_width(card, 10, 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_30, 0);
  lv_obj_set_style_shadow_spread(card, -3, 0);
  lv_obj_set_style_pad_all(card, 8, 0);
  return card;
}

static lv_obj_t *lab_label(lv_obj_t *parent,
                           const char *text,
                           const lv_font_t *font,
                           lv_color_t color,
                           lv_align_t align,
                           int x,
                           int y) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, color, 0);
  lv_obj_align(label, align, x, y);
  return label;
}

static const char *lab_state_name(lora_lab_state_t state) {
  switch (state) {
    case LORA_LAB_STARTING:
      return "STARTING";
    case LORA_LAB_RX:
      return "RX PASS";
    case LORA_LAB_FAILED:
      return "FAILED";
    default:
      return "IDLE";
  }
}

static void format_packet_prefix(const lora_lab_snapshot_t *snap, char *out, size_t out_len) {
  if (snap->last_packet_len == 0) {
    snprintf(out, out_len, "Last: waiting for LoRa packet");
    return;
  }
  size_t shown = snap->last_packet_len;
  if (shown > sizeof(snap->last_packet_prefix)) {
    shown = sizeof(snap->last_packet_prefix);
  }
  size_t used = (size_t)snprintf(out, out_len, "HEX:");
  for (size_t i = 0; i < shown && used + 4 < out_len; i++) {
    used += (size_t)snprintf(out + used, out_len - used, " %02X", snap->last_packet_prefix[i]);
  }
  if (snap->last_packet_len > shown && used + 4 < out_len) {
    snprintf(out + used, out_len - used, " ...");
  }
}

static void refresh_readouts(void) {
  lora_lab_snapshot_t snap = {0};
  lora_lab_get_snapshot(&snap);

  lv_color_t state_color = current_theme.text_secondary;
  if (snap.state == LORA_LAB_RX) {
    state_color = lv_color_hex(UI_COL_SUCCESS);
  } else if (snap.state == LORA_LAB_FAILED) {
    state_color = lv_color_hex(UI_COL_DANGER);
  } else if (snap.state == LORA_LAB_STARTING) {
    state_color = ui_theme_get_accent();
  }
  lv_label_set_text_fmt(
      s_state_label, "%s   %s", lab_state_name(snap.state), LAB_FREQUENCIES[s_freq_index].label);
  lv_obj_set_style_text_color(s_state_label, state_color, 0);

  if (snap.state == LORA_LAB_FAILED) {
    lv_label_set_text_fmt(s_chip_label, "SPI/SX1262: %s", esp_err_to_name(snap.error));
  } else if (snap.state == LORA_LAB_RX) {
    lv_label_set_text_fmt(s_chip_label,
                          "SPI 0x%02X  ERR %04X  B%d D%d",
                          snap.chip_status,
                          snap.device_errors,
                          snap.busy_level,
                          snap.dio1_level);
  } else {
    lv_label_set_text(s_chip_label, "SPI + BUSY + DIO1 check...");
  }

  if (snap.rssi_samples == 0) {
    lv_label_set_text(s_rssi_label, "RSSI  -- dBm");
    lv_label_set_text(s_rssi_stats_label, "min / avg / max:  -- / -- / --");
    lv_bar_set_value(s_rssi_bar, -130, LV_ANIM_OFF);
  } else {
    lv_label_set_text_fmt(s_rssi_label, "RSSI  %d dBm", snap.rssi_now_dbm);
    lv_label_set_text_fmt(s_rssi_stats_label,
                          "min / avg / max:  %d / %d / %d",
                          snap.rssi_min_dbm,
                          snap.rssi_avg_dbm,
                          snap.rssi_max_dbm);
    int value = snap.rssi_now_dbm;
    if (value < -130)
      value = -130;
    if (value > -30)
      value = -30;
    lv_bar_set_value(s_rssi_bar, value, LV_ANIM_ON);
  }

  lv_label_set_text_fmt(s_packet_label,
                        "RX %lu   CRC %u   HDR %u",
                        (unsigned long)snap.rx_packets,
                        snap.crc_errors,
                        snap.header_errors);
  char last[96];
  format_packet_prefix(&snap, last, sizeof(last));
  if (snap.last_packet_len > 0) {
    size_t used = strlen(last);
    if (used + 24 < sizeof(last)) {
      snprintf(last + used,
               sizeof(last) - used,
               "\n%dB  %ddBm  SNR %d",
               snap.last_packet_len,
               snap.last_packet_rssi_dbm,
               snap.last_packet_snr_db);
    }
  }
  lv_label_set_text(s_last_label, last);
}

static void refresh_timer_cb(lv_timer_t *timer) {
  if (lv_screen_active() != s_screen) {
    lora_lab_stop();
    lv_timer_delete(timer);
    if (s_refresh_timer == timer) {
      s_refresh_timer = NULL;
    }
    return;
  }
  refresh_readouts();
}

static void start_selected_frequency(void) {
  lora_lab_stop();
  esp_err_t err = lora_lab_start(LAB_FREQUENCIES[s_freq_index].hz);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "start failed: %s", esp_err_to_name(err));
    notify(NOTIFY_LORA, "Radio Lab start failed");
  }
  refresh_readouts();
}

static void lora_lab_input(const input_event_t *event, void *ctx) {
  (void)ctx;
  if (event->action != INPUT_ACTION_PRESS) {
    return;
  }

  switch (event->button) {
    case INPUT_BTN_LEFT:
      s_freq_index = (s_freq_index + LAB_FREQ_COUNT - 1) % LAB_FREQ_COUNT;
      ui_feedback(UI_FB_SELECT);
      start_selected_frequency();
      break;
    case INPUT_BTN_RIGHT:
      s_freq_index = (s_freq_index + 1) % LAB_FREQ_COUNT;
      ui_feedback(UI_FB_SELECT);
      start_selected_frequency();
      break;
    case INPUT_BTN_OK:
      lora_lab_reset_stats();
      ui_feedback(UI_FB_SELECT);
      notify(NOTIFY_LORA, "LoRa counters reset");
      refresh_readouts();
      break;
    case INPUT_BTN_BACK:
      lora_lab_stop();
      ui_switch_screen(SCREEN_LORA_CHAT);
      break;
    default:
      break;
  }
}

static void build_screen(void) {
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);

  lv_color_t accent = ui_theme_get_accent();
  ui_chrome_header(s_screen, "LORA RADIO LAB", ICON_ANTENNA);

  lv_obj_t *status = lab_card(s_screen, BODY_TOP_Y, STATUS_H, accent);
  s_state_label =
      lab_label(status, "STARTING", &lv_font_montserrat_14, accent, LV_ALIGN_TOP_LEFT, 0, 0);
  s_chip_label = lab_label(status,
                           "SPI + BUSY + DIO1 check...",
                           &lv_font_montserrat_12,
                           current_theme.text_secondary,
                           LV_ALIGN_BOTTOM_LEFT,
                           0,
                           0);

  int rssi_y = BODY_TOP_Y + STATUS_H + CARD_GAP;
  lv_obj_t *signal = lab_card(s_screen, rssi_y, RSSI_H, accent);
  s_rssi_label = lab_label(signal,
                           "RSSI  -- dBm",
                           &lv_font_montserrat_16,
                           current_theme.text_main,
                           LV_ALIGN_TOP_LEFT,
                           0,
                           0);
  s_rssi_stats_label = lab_label(signal,
                                 "min / avg / max:  -- / -- / --",
                                 &lv_font_montserrat_12,
                                 current_theme.text_secondary,
                                 LV_ALIGN_TOP_LEFT,
                                 0,
                                 24);
  s_rssi_bar = lv_bar_create(signal);
  lv_obj_set_size(s_rssi_bar, 198, 7);
  lv_obj_align(s_rssi_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_bar_set_range(s_rssi_bar, -130, -30);
  lv_bar_set_value(s_rssi_bar, -130, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_rssi_bar, current_theme.bg_primary, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_rssi_bar, accent, LV_PART_INDICATOR);

  int packet_y = rssi_y + RSSI_H + CARD_GAP;
  lv_obj_t *packets = lab_card(s_screen, packet_y, PACKET_H, accent);
  s_packet_label = lab_label(packets,
                             "RX 0   CRC 0   HDR 0",
                             &lv_font_montserrat_12,
                             lv_color_hex(UI_COL_SUCCESS),
                             LV_ALIGN_TOP_LEFT,
                             0,
                             0);
  s_last_label = lab_label(packets,
                           "Last: waiting for LoRa packet",
                           &lv_font_montserrat_12,
                           current_theme.text_secondary,
                           LV_ALIGN_TOP_LEFT,
                           0,
                           20);

  ui_chrome_footer(s_screen, LV_SYMBOL_LEFT LV_SYMBOL_RIGHT " freq  OK reset  BACK");
  lv_obj_fade_in(s_screen, ENTRY_MS, 0);
}

void ui_lora_rnode_open(void) {
  ui_theme_set_protocol(PROTOCOL_LORA);
  build_screen();
  ui_input_set_screen_handler(lora_lab_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);

  start_selected_frequency();
  if (s_refresh_timer == NULL) {
    s_refresh_timer = lv_timer_create(refresh_timer_cb, LAB_REFRESH_MS, NULL);
  }
  ESP_LOGI(TAG, "real receive-only SX1262 lab opened");
}
