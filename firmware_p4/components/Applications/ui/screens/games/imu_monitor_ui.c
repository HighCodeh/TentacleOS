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

#include "imu_monitor_ui.h"

#include "esp_err.h"
#include "lvgl.h"

#include "qmi8658a.h"
#include "ui_chrome.h"
#include "ui_feedback.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define SAMPLE_MS 100

#define HDR_TITLE "MOTION / IMU"
#define HDR_ICON  "/assets/icons/sensors.bin"

static const uint8_t ODR_BITS[] = {8, 7, 6, 5};
static const char *const ODR_NAMES[] = {"28 Hz", "56 Hz", "112 Hz", "224 Hz"};
#define ODR_COUNT 4

static lv_obj_t *s_screen;
static lv_timer_t *s_timer;
static lv_obj_t *s_status;
static lv_obj_t *s_rate;
static lv_obj_t *s_accel[3];
static lv_obj_t *s_gyro[3];
static lv_obj_t *s_temp;

static qmi8658a_sample_t s_last;
static bool s_have_sample;
static float s_accel_zero[3];
static float s_gyro_zero[3];
static int s_odr_idx = 2;

static lv_obj_t *make_card(int y, int h, const char *title) {
  lv_obj_t *card = lv_obj_create(s_screen);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(card, 224, h);
  lv_obj_align(card, LV_ALIGN_TOP_MID, 0, y);
  lv_obj_set_style_radius(card, 10, 0);
  lv_obj_set_style_bg_color(card, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(card, current_theme.border_accent, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_pad_all(card, 8, 0);

  lv_obj_t *label = lv_label_create(card);
  lv_label_set_text(label, title);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(label, current_theme.border_accent, 0);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);
  return card;
}

static lv_obj_t *make_value(lv_obj_t *card, int x, int y) {
  lv_obj_t *label = lv_label_create(card);
  lv_label_set_text(label, "--");
  lv_obj_set_width(label, 66);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(label, current_theme.text_main, 0);
  lv_obj_align(label, LV_ALIGN_TOP_LEFT, x, y);
  return label;
}

static void show_error(esp_err_t err) {
  if (s_status == NULL)
    return;
  if (err == ESP_ERR_NOT_FOUND)
    lv_label_set_text(s_status, "QMI8658A NOT DETECTED");
  else
    lv_label_set_text_fmt(s_status, "IMU ERROR 0x%X", (unsigned)err);
  lv_obj_set_style_text_color(s_status, lv_color_hex(0xFF5470), 0);
}

static void refresh_sample(void) {
  qmi8658a_sample_t sample;
  esp_err_t err = qmi8658a_read(&sample);
  if (err != ESP_OK) {
    show_error(err);
    return;
  }
  s_last = sample;
  s_have_sample = true;

  lv_label_set_text_fmt(s_accel[0], "X %+1.3f g", sample.accel_g[0] - s_accel_zero[0]);
  lv_label_set_text_fmt(s_accel[1], "Y %+1.3f g", sample.accel_g[1] - s_accel_zero[1]);
  lv_label_set_text_fmt(s_accel[2], "Z %+1.3f g", sample.accel_g[2] - s_accel_zero[2]);
  lv_label_set_text_fmt(s_gyro[0], "X %+1.1f", sample.gyro_dps[0] - s_gyro_zero[0]);
  lv_label_set_text_fmt(s_gyro[1], "Y %+1.1f", sample.gyro_dps[1] - s_gyro_zero[1]);
  lv_label_set_text_fmt(s_gyro[2], "Z %+1.1f", sample.gyro_dps[2] - s_gyro_zero[2]);
  lv_label_set_text_fmt(s_temp, "DIE  %.1f C", sample.temperature_c);
  lv_label_set_text_fmt(s_status,
                        "LIVE ID 05 REV %02X %s",
                        qmi8658a_revision(),
                        (sample.status & 0x03) == 0x03 ? "DRDY" : "WAIT");
  lv_obj_set_style_text_color(s_status, lv_color_hex(0x37E0A8), 0);
}

static void sample_timer_cb(lv_timer_t *timer) {
  (void)timer;
  if (lv_screen_active() == s_screen)
    refresh_sample();
}

static void screen_delete_cb(lv_event_t *event) {
  (void)event;
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  (void)qmi8658a_stop();
  s_screen = NULL;
}

static void imu_input(const input_event_t *event, void *ctx) {
  (void)ctx;
  if (event->action != INPUT_ACTION_PRESS)
    return;
  switch (event->button) {
    case INPUT_BTN_BACK:
      ui_switch_screen(SCREEN_SETTINGS);
      break;
    case INPUT_BTN_OK:
      if (s_have_sample) {
        s_accel_zero[0] = s_last.accel_g[0];
        s_accel_zero[1] = s_last.accel_g[1];
        s_accel_zero[2] = s_last.accel_g[2] - (s_last.accel_g[2] >= 0.0f ? 1.0f : -1.0f);
        for (int i = 0; i < 3; i++)
          s_gyro_zero[i] = s_last.gyro_dps[i];
        ui_feedback(UI_FB_SELECT);
        refresh_sample();
      }
      break;
    case INPUT_BTN_RIGHT:
      s_odr_idx = (s_odr_idx + 1) % ODR_COUNT;
      if (qmi8658a_set_odr(ODR_BITS[s_odr_idx]) == ESP_OK) {
        lv_label_set_text(s_rate, ODR_NAMES[s_odr_idx]);
        ui_feedback(UI_FB_NAV);
      }
      break;
    default:
      break;
  }
}

void ui_imu_monitor_open(void) {
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_have_sample = false;
  for (int i = 0; i < 3; i++) {
    s_accel_zero[i] = 0.0f;
    s_gyro_zero[i] = 0.0f;
  }

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  lv_obj_add_event_cb(s_screen, screen_delete_cb, LV_EVENT_DELETE, NULL);

  ui_chrome_header(s_screen, HDR_TITLE, HDR_ICON);

  s_status = lv_label_create(s_screen);
  lv_label_set_text(s_status, "PROBING QMI8658A...");
  lv_obj_set_style_text_font(s_status, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_status, current_theme.text_secondary, 0);
  lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 8, 48);

  s_rate = lv_label_create(s_screen);
  lv_label_set_text(s_rate, ODR_NAMES[s_odr_idx]);
  lv_obj_set_style_text_font(s_rate, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(s_rate, current_theme.border_accent, 0);
  lv_obj_align(s_rate, LV_ALIGN_TOP_RIGHT, -8, 48);

  lv_obj_t *accel = make_card(70, 78, "ACCELEROMETER");
  lv_obj_t *gyro = make_card(155, 78, "GYROSCOPE  dps");
  for (int i = 0; i < 3; i++) {
    s_accel[i] = make_value(accel, i * 69, 30);
    s_gyro[i] = make_value(gyro, i * 69, 30);
  }

  lv_obj_t *temp_card = make_card(240, 38, "");
  s_temp = make_value(temp_card, 0, 3);
  lv_obj_set_width(s_temp, 180);

  ui_chrome_footer(s_screen, "OK Zero   RIGHT Rate   BACK");
  ui_input_set_screen_handler(imu_input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);

  esp_err_t err = qmi8658a_start();
  if (err == ESP_OK) {
    (void)qmi8658a_set_odr(ODR_BITS[s_odr_idx]);
    refresh_sample();
    s_timer = lv_timer_create(sample_timer_cb, SAMPLE_MS, NULL);
  } else {
    show_error(err);
  }
}
