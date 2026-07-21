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

#include "power_ui.h"

#include <stdio.h>

#include "driver/i2c.h"
#include "lvgl.h"

#include "bq25896.h"
#include "buttons_gpio.h"
#include "msgbox_ui.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_theme.h"

#define NAV_TIMER_MS 60
#define REFRESH_MS   700

enum { ACT_CHARGE, ACT_SCAN, ACT_REGS, ACT_OFF, ACT_COUNT };
static const char *const ACT_NAMES[ACT_COUNT] = {
    "Charging: --", "I2C Scan", "Registers", "Power Off"};

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_telem = NULL;
static lv_obj_t *s_chart = NULL;
static lv_chart_series_t *s_series = NULL;
static lv_obj_t *s_acts[ACT_COUNT];
static lv_timer_t *s_timer = NULL;
static int s_sel = 0;
static uint32_t s_last_refresh = 0;
static char s_scan_msg[96];
static bool s_up_last, s_down_last, s_ok_last, s_back_last, s_left_last, s_right_last;

static const char *chg_name(bq25896_charge_status_t s) {
  switch (s) {
    case CHARGE_STATUS_PRECHARGE:
      return "Pre-charge";
    case CHARGE_STATUS_FAST_CHARGE:
      return "Fast charge";
    case CHARGE_STATUS_CHARGE_DONE:
      return "Charge done";
    default:
      return "Not charging";
  }
}
static const char *vbus_name(bq25896_vbus_status_t s) {
  switch (s) {
    case VBUS_STATUS_USB_HOST:
      return "USB";
    case VBUS_STATUS_ADAPTER_PORT:
      return "Adapter";
    case VBUS_STATUS_OTG:
      return "OTG";
    default:
      return "None";
  }
}

static void refresh_telem(void) {
  bq25896_telem_t t;
  if (bq25896_read_telemetry(&t) != ESP_OK) {
    lv_label_set_text(s_telem, "BQ25896 not responding\n(check I2C / 0x6B)");
    return;
  }
  char vb[16];
  if (t.power_good)
    snprintf(vb,
             sizeof(vb),
             "%u.%02uV %s",
             t.vbus_mv / 1000,
             (t.vbus_mv % 1000) / 10,
             vbus_name(t.vbus));
  else
    snprintf(vb, sizeof(vb), "none");

  const char *status;
  if (t.charging) {
    status = chg_name(t.chg);
  } else if (!t.power_good) {
    status = "On battery";
  } else {
    uint8_t r00 = bq25896_reg_raw(0x00);
    uint8_t f = t.fault;
    if (r00 & 0x80)
      status = "Idle (HiZ)";
    else if (!bq25896_get_charge_enable())
      status = "Idle (off)";
    else if (f & 0x80)
      status = "Fault: WD";
    else if (f & 0x40)
      status = "Fault: boost";
    else if ((f & 0x30) == 0x10)
      status = "Fault: input";
    else if ((f & 0x30) == 0x20)
      status = "Fault: thermal";
    else if ((f & 0x30) == 0x30)
      status = "Fault: timer";
    else if (f & 0x08)
      status = "Fault: batt OVP";
    else if (f & 0x07)
      status = "Fault: NTC";
    else
      status = "Idle (full?)";
  }

  char buf[160];
  snprintf(buf,
           sizeof(buf),
           "Batt %u.%02u V   %d%%   %s\n"
           "Sys %u.%02u V    VBUS %s\n"
           "Chg %u mA   In %u mA   F:%02X",
           t.vbat_mv / 1000,
           (t.vbat_mv % 1000) / 10,
           t.soc,
           status,
           t.vsys_mv / 1000,
           (t.vsys_mv % 1000) / 10,
           vb,
           t.ichg_ma,
           t.iinlim_ma,
           t.fault);
  lv_label_set_text(s_telem, buf);

  if (s_chart && s_series)
    lv_chart_set_next_value(s_chart, s_series, t.soc);

  lv_label_set_text_fmt(
      s_acts[ACT_CHARGE], "Charging: %s", bq25896_get_charge_enable() ? "ON" : "OFF");
}

static void draw_selection(void) {
  for (int i = 0; i < ACT_COUNT; i++) {
    bool sel = (i == s_sel);
    lv_obj_set_style_text_color(
        s_acts[i], sel ? ui_theme_get_accent() : current_theme.text_main, 0);
    lv_obj_set_style_text_opa(s_acts[i], sel ? LV_OPA_COVER : LV_OPA_70, 0);
  }
}

static void i2c_scan_fill(void) {
  int n = 0;
  s_scan_msg[0] = '\0';
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (a << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t r = i2c_master_cmd_begin(I2C_NUM_0, cmd, 20 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(cmd);
    if (r == ESP_OK && n < (int)sizeof(s_scan_msg) - 7)
      n += snprintf(s_scan_msg + n, sizeof(s_scan_msg) - n, "0x%02X ", a);
  }
  if (n == 0)
    snprintf(s_scan_msg, sizeof(s_scan_msg), "No I2C devices found");
}

static void poweroff_cb(bool confirm) {
  if (confirm)
    bq25896_power_off();
}

static void do_action(int act) {
  if (act == ACT_CHARGE) {
    bq25896_set_charge_enable(!bq25896_get_charge_enable());
  } else if (act == ACT_SCAN) {
    i2c_scan_fill();
    msgbox_open(LV_SYMBOL_LIST, s_scan_msg, NULL, NULL, NULL);
  } else if (act == ACT_REGS) {
    uint8_t ts = bq25896_reg_raw(0x10) & 0x7F;
    int tsx10 = 210 + ts * 465 / 100;
    uint8_t f = bq25896_reg_raw(0x0C);
    const char *ts_hint = (tsx10 < 344)   ? "TS low: short? (Hot)"
                          : (tsx10 > 732) ? "TS high: open? (Cold)"
                                          : "TS in range (OK)";
    snprintf(s_scan_msg,
             sizeof(s_scan_msg),
             "TS %d.%d%% (~50 ok)\n%s\n0B:%02X 0C:%02X NTC:%X\n00:%02X 04:%02X 0D:%02X",
             tsx10 / 10,
             tsx10 % 10,
             ts_hint,
             bq25896_reg_raw(0x0B),
             f,
             f & 0x07,
             bq25896_reg_raw(0x00),
             bq25896_reg_raw(0x04),
             bq25896_reg_raw(0x0D));
    msgbox_open(LV_SYMBOL_SETTINGS, s_scan_msg, NULL, NULL, NULL);
  } else if (act == ACT_OFF) {
    msgbox_open(LV_SYMBOL_POWER,
                "Power off the HighBoy?\n(unplug USB to stay off)",
                "Off",
                "Cancel",
                poweroff_cb);
  }
}

static void tick_cb(lv_timer_t *t) {
  if (lv_screen_active() != s_screen) {
    lv_timer_delete(t);
    s_timer = NULL;
    return;
  }
  bool up = ui_btn_up(), down = ui_btn_down();
  bool left = ui_btn_left(), right = ui_btn_right();
  bool ok = ok_button_is_down(), back = back_button_is_down();

  if (msgbox_is_open() || ui_input_is_locked()) {
    s_up_last = up;
    s_down_last = down;
    s_left_last = left;
    s_right_last = right;
    s_ok_last = ok;
    s_back_last = back;
    return;
  }

  if ((back && !s_back_last) || (left && !s_left_last)) {
    ui_switch_screen(SCREEN_SETTINGS);
    return;
  }
  if (down && !s_down_last) {
    s_sel = (s_sel + 1) % ACT_COUNT;
    draw_selection();
  }
  if (up && !s_up_last) {
    s_sel = (s_sel - 1 + ACT_COUNT) % ACT_COUNT;
    draw_selection();
  }
  if ((ok && !s_ok_last) || (right && !s_right_last))
    do_action(s_sel);

  if (lv_tick_get() - s_last_refresh >= REFRESH_MS) {
    s_last_refresh = lv_tick_get();
    refresh_telem();
  }

  s_up_last = up;
  s_down_last = down;
  s_left_last = left;
  s_right_last = right;
  s_ok_last = ok;
  s_back_last = back;
}

void ui_power_open(void) {
  bq25896_init();
  if (s_screen != NULL) {
    lv_obj_del(s_screen);
    s_screen = NULL;
  }
  s_sel = 0;
  s_last_refresh = 0;
  s_chart = NULL;
  s_series = NULL;
  s_up_last = s_down_last = s_ok_last = s_back_last = s_left_last = s_right_last = false;

  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

  ui_chrome_header(s_screen, "Power", "/assets/icons/power_icon.bin");

  s_telem = lv_label_create(s_screen);
  lv_label_set_text(s_telem, "Reading...");
  lv_obj_set_style_text_color(s_telem, current_theme.text_main, 0);
  lv_obj_set_style_text_font(s_telem, &lv_font_montserrat_12, 0);
  lv_obj_align(s_telem, LV_ALIGN_TOP_LEFT, 10, 48);

  s_chart = lv_chart_create(s_screen);
  lv_obj_set_size(s_chart, lv_pct(92), 72);
  lv_obj_align(s_chart, LV_ALIGN_TOP_MID, 0, 106);
  lv_obj_set_style_bg_color(s_chart, current_theme.bg_secondary, 0);
  lv_obj_set_style_bg_opa(s_chart, LV_OPA_40, 0);
  lv_obj_set_style_border_width(s_chart, 1, 0);
  lv_obj_set_style_border_color(s_chart, current_theme.border_interface, 0);
  lv_obj_set_style_radius(s_chart, 6, 0);
  lv_obj_set_style_width(s_chart, 0, LV_PART_INDICATOR);
  lv_obj_set_style_height(s_chart, 0, LV_PART_INDICATOR);
  lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_update_mode(s_chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_point_count(s_chart, 40);
  lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  lv_chart_set_div_line_count(s_chart, 3, 0);
  s_series = lv_chart_add_series(s_chart, ui_theme_get_accent(), LV_CHART_AXIS_PRIMARY_Y);

  lv_obj_t *gl = lv_label_create(s_screen);
  lv_label_set_text(gl, "Battery %");
  lv_obj_set_style_text_color(gl, current_theme.text_main, 0);
  lv_obj_set_style_text_opa(gl, LV_OPA_50, 0);
  lv_obj_set_style_text_font(gl, &lv_font_montserrat_12, 0);
  lv_obj_align(gl, LV_ALIGN_TOP_MID, 0, 94);

  for (int i = 0; i < ACT_COUNT; i++) {
    s_acts[i] = lv_label_create(s_screen);
    lv_label_set_text(s_acts[i], ACT_NAMES[i]);
    lv_obj_set_style_text_color(s_acts[i], current_theme.text_main, 0);
    lv_obj_set_style_text_font(s_acts[i], &lv_font_montserrat_14, 0);
    lv_obj_align(s_acts[i], LV_ALIGN_BOTTOM_LEFT, 12, -100 + i * 22);
  }
  draw_selection();
  refresh_telem();

  ui_chrome_footer(s_screen, "UP/DOWN select   OK do   BACK exit");

  if (s_timer == NULL)
    s_timer = lv_timer_create(tick_cb, NAV_TIMER_MS, NULL);

  lv_screen_load(s_screen);
}
