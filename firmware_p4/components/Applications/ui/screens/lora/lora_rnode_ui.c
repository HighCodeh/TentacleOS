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
#include "rf_bench_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "led_control.h"
#include "lora_session.h"
#include "menu_component_ui.h"
#include "notify_ui.h"
#include "rf_chamber.h"
#include "rf_passive.h"
#include "esp_system.h"
#include "power_policy.h"
#include "ui_chrome.h"
#include "ui_manager.h"
#include "ui_metrics.h"
#include "ui_theme.h"

#define REFRESH_MS    250U
#define MHZ           1000000U
#define MAX_ROWS      15
#define BODY_TOP      48
#define BODY_MARGIN   8
#define HIGH_CW_POWER 14
#define HIGH_CW_MS    1000U
static const char *TAG = "RF_LAB_UI";
static const char *MODES[] = {"CW carrier", "TX packets", "Receive"};
static const int8_t CC_POWERS[] = {-30, -20, -10, 0, 5, 10};
static const uint32_t STEPS[] = {1000, 10000, 100000, 1000000};
static const uint32_t FREQUENCIES[] = {315000000, 433920000, 868100000, 915000000};
static const uint32_t DURATIONS[] = {1000, 5000, 10000, 30000, 60000, 120000};
static const uint32_t INTERVALS[] = {250, 500, 1000, 2000, 5000, 10000, 30000, 60000};
typedef enum {
  FIELD_START,
  FIELD_RADIO,
  FIELD_MODE,
  FIELD_FREQUENCY,
  FIELD_STEP,
  FIELD_POWER,
  FIELD_PATH,
  FIELD_SF,
  FIELD_BW,
  FIELD_CR,
  FIELD_DURATION,
  FIELD_INTERVAL,
  FIELD_MESH_STOP,
  FIELD_PASSIVE,
  FIELD_QUICK,
  FIELD_COUNT
} rf_lab_field_t;
static const char *NAMES[] = {"Start",
                              "Radio",
                              "Mode",
                              "Freq",
                              "Step",
                              "Power",
                              "Branch",
                              "SF",
                              "BW",
                              "Code rate",
                              "Duration",
                              "Interval",
                              "Stop mesh",
                              "Passive / restart",
                              "Quick setup"};
typedef struct {
  const char *name;
  uint32_t hz;
  uint8_t radio, mode, path;
} rf_lab_preset_t;
static const rf_lab_preset_t PRESETS[] = {
    {"Custom", 0, 0, 0, 0},
    {"Sub 315 carrier", 315000000, RF_CHAMBER_CC, RF_CHAMBER_CW, 1},
    {"Sub 433 carrier", 433920000, RF_CHAMBER_CC, RF_CHAMBER_CW, 1},
    {"Sub 868 carrier", 868350000, RF_CHAMBER_CC, RF_CHAMBER_CW, 2},
    {"Sub 915 carrier", 915000000, RF_CHAMBER_CC, RF_CHAMBER_CW, 2},
    {"LoRa 868 carrier", 868100000, RF_CHAMBER_SX, RF_CHAMBER_CW, 0},
    {"LoRa 915 carrier", 915000000, RF_CHAMBER_SX, RF_CHAMBER_CW, 0},
    {"LoRa 868 packets", 868100000, RF_CHAMBER_SX, RF_CHAMBER_TX, 0},
    {"LoRa 915 packets", 915000000, RF_CHAMBER_SX, RF_CHAMBER_TX, 0},
    {"Sub 433 packets", 433920000, RF_CHAMBER_CC, RF_CHAMBER_TX, 1},
    {"Sub 868 packets", 868350000, RF_CHAMBER_CC, RF_CHAMBER_TX, 2},
    {"Sub 915 packets", 915000000, RF_CHAMBER_CC, RF_CHAMBER_TX, 2},
};
#define PRESET_COUNT (sizeof(PRESETS) / sizeof(PRESETS[0]))
static const char *BRANCHES[] = {"L16 / 51 nH", "L17 / 16 nH", "L18 / 1.8 nH", "L19 / 1.5 nH"};
static uint8_t s_preset;
static bool s_exit_passive;
static lv_obj_t *s_screen;
static lv_obj_t *s_readout;
static lv_obj_t *s_body;
static bool s_from_subghz = true;
static lv_timer_t *s_timer;
static menu_component_t s_menu;
static rf_lab_field_t s_rows[MAX_ROWS];
static uint8_t s_row_count;
static uint8_t s_step;
static uint32_t s_owned_run;
static bool s_is_live;
static bool s_is_leaving;
static bool s_mesh_pending;
static screen_id_t s_return = SCREEN_MENU;
static rf_chamber_config_t s_config = {.radio = RF_CHAMBER_CC,
                                       .mode = RF_CHAMBER_RX,
                                       .frequency_hz = 433920000,
                                       .power_dbm = -30,
                                       .path = 1,
                                       .sf = 7,
                                       .bandwidth_khz = 125,
                                       .cr = 1,
                                       .duration_ms = 5000,
                                       .interval_ms = 1000};
static void build(bool live);
static void input(const input_event_t *event, void *ctx);
static void refresh(lv_timer_t *timer);

static uint32_t cycle(const uint32_t *values, size_t count, uint32_t value, int direction) {
  size_t index = 0;
  while (index < count && values[index] != value)
    ++index;
  if (index == count)
    index = 0;
  return values[(index + count + direction) % count];
}
static void normalize(void) {
  if (s_config.mode == RF_CHAMBER_CW) {
    uint32_t limit = s_config.radio == RF_CHAMBER_SX && s_config.power_dbm > HIGH_CW_POWER
                         ? HIGH_CW_MS
                         : RF_CHAMBER_MAX_CW_MS;
    if (s_config.duration_ms > limit)
      s_config.duration_ms = limit;
  }
}
static void value(rf_lab_field_t field, char *out, size_t size) {
  switch (field) {
    case FIELD_QUICK:
      snprintf(out, size, "%s", PRESETS[s_preset].name);
      break;
    case FIELD_RADIO:
      snprintf(out, size, "%s", s_config.radio == RF_CHAMBER_CC ? "Sub-GHz" : "LoRa");
      break;
    case FIELD_MODE:
      snprintf(out, size, "%s", MODES[s_config.mode]);
      break;
    case FIELD_FREQUENCY:
      snprintf(out,
               size,
               "%lu.%06lu",
               (unsigned long)(s_config.frequency_hz / MHZ),
               (unsigned long)(s_config.frequency_hz % MHZ));
      break;
    case FIELD_STEP:
      snprintf(out, size, "%lu kHz", (unsigned long)(STEPS[s_step] / 1000));
      break;
    case FIELD_POWER:
      snprintf(out, size, "%+d dBm", s_config.power_dbm);
      break;
    case FIELD_PATH:
      snprintf(out, size, "RF%u", s_config.path);
      break;
    case FIELD_SF:
      snprintf(out, size, "%u", s_config.sf);
      break;
    case FIELD_BW:
      snprintf(out, size, "%u kHz", s_config.bandwidth_khz);
      break;
    case FIELD_CR:
      snprintf(out, size, "4/%u", s_config.cr + 4);
      break;
    case FIELD_DURATION:
      snprintf(out, size, "%lu s", (unsigned long)(s_config.duration_ms / 1000));
      break;
    case FIELD_INTERVAL:
      snprintf(out, size, "%lu ms", (unsigned long)s_config.interval_ms);
      break;
    default:
      out[0] = 0;
      break;
  }
}
static void adjust(rf_lab_field_t field, int direction, bool preset) {
  if (field != FIELD_QUICK)
    s_preset = 0;
  switch (field) {
    case FIELD_QUICK: {
      s_preset = (s_preset + PRESET_COUNT + direction) % PRESET_COUNT;
      if (s_preset == 0)
        break;
      const rf_lab_preset_t *p = &PRESETS[s_preset];
      s_config = (rf_chamber_config_t){.radio = p->radio,
                                       .mode = p->mode,
                                       .frequency_hz = p->hz,
                                       .power_dbm = p->radio == RF_CHAMBER_CC ? -30 : -9,
                                       .path = p->path,
                                       .sf = 7,
                                       .bandwidth_khz = 125,
                                       .cr = 1,
                                       .duration_ms = p->mode == RF_CHAMBER_CW ? 1000 : 5000,
                                       .interval_ms = 1000};
      break;
    }
    case FIELD_RADIO:
      s_config.radio = s_config.radio == RF_CHAMBER_CC ? RF_CHAMBER_SX : RF_CHAMBER_CC;
      s_config.frequency_hz = s_config.radio == RF_CHAMBER_CC ? FREQUENCIES[1] : FREQUENCIES[2];
      s_config.power_dbm = s_config.radio == RF_CHAMBER_CC ? CC_POWERS[0] : -9;
      s_config.path = s_config.radio == RF_CHAMBER_CC ? 1 : 0;
      s_config.sf = 7;
      s_config.bandwidth_khz = 125;
      s_config.cr = 1;
      break;
    case FIELD_MODE:
      s_config.mode = (s_config.mode + RF_CHAMBER_MODE_COUNT + direction) % RF_CHAMBER_MODE_COUNT;
      break;
    case FIELD_FREQUENCY: {
      rf_chamber_config_t candidate = s_config;
      if (preset) {
        candidate.frequency_hz = cycle(FREQUENCIES, 4, candidate.frequency_hz, 1);
        if (candidate.radio == RF_CHAMBER_SX && candidate.frequency_hz < FREQUENCIES[2])
          candidate.frequency_hz = FREQUENCIES[2];
      } else {
        candidate.frequency_hz += direction * STEPS[s_step];
      }
      if (rf_chamber_validate(&candidate) == ESP_OK)
        s_config = candidate;
      break;
    }
    case FIELD_STEP:
      s_step = (s_step + 4 + direction) % 4;
      break;
    case FIELD_POWER:
      if (s_config.radio == RF_CHAMBER_SX)
        s_config.power_dbm = (s_config.power_dbm + 9 + 32 + direction) % 32 - 9;
      else {
        unsigned index = 0;
        while (index + 1 < sizeof(CC_POWERS) && CC_POWERS[index] != s_config.power_dbm)
          ++index;
        s_config.power_dbm = CC_POWERS[(index + sizeof(CC_POWERS) + direction) % sizeof(CC_POWERS)];
      }
      break;
    case FIELD_PATH:
      s_config.path = (s_config.path - 1 + 4 + direction) % 4 + 1;
      break;
    case FIELD_SF:
      s_config.sf = (s_config.sf - 7 + 6 + direction) % 6 + 7;
      break;
    case FIELD_BW: {
      static const uint32_t widths[] = {125, 250, 500};
      s_config.bandwidth_khz = cycle(widths, 3, s_config.bandwidth_khz, direction);
      break;
    }
    case FIELD_CR:
      s_config.cr = (s_config.cr - 1 + 4 + direction) % 4 + 1;
      break;
    case FIELD_DURATION:
      s_config.duration_ms = cycle(DURATIONS, 6, s_config.duration_ms, direction);
      break;
    case FIELD_INTERVAL:
      s_config.interval_ms = cycle(INTERVALS, 8, s_config.interval_ms, direction);
      break;
    default:
      break;
  }
  normalize();
}
static void stop_owned(void) {
  rf_chamber_status_t status;
  rf_chamber_snapshot(&status);
  if (s_owned_run != 0 && status.run_id == s_owned_run && status.phase != RF_CHAMBER_IDLE)
    rf_chamber_stop();
}
void ui_lora_rnode_close(void) {
  ui_rf_bench_close();
  stop_owned();
  if (s_timer != NULL) {
    lv_timer_delete(s_timer);
    s_timer = NULL;
  }
}
static void deleted(lv_event_t *event) {
  if (lv_event_get_target(event) != s_screen)
    return;
  ui_lora_rnode_close();
  s_screen = s_readout = s_body = NULL;
}
static void show_error(esp_err_t error) {
  ESP_LOGW(TAG, "Request failed: %s", esp_err_to_name(error));
  led_signal_warning();
  notify(NOTIFY_WARNING, esp_err_to_name(error));
}
static void start(void) {
  if (s_timer == NULL) {
    show_error(ESP_ERR_NO_MEM);
    return;
  }
  rf_chamber_status_t status;
  rf_chamber_snapshot(&status);
  if (status.phase != RF_CHAMBER_IDLE) {
    show_error(ESP_ERR_INVALID_STATE);
    return;
  }
  if (s_config.radio == RF_CHAMBER_SX && lora_session_active() != LORA_PROTO_NONE) {
    notify(NOTIFY_WARNING, "Use Stop mesh first");
    return;
  }
  esp_err_t error = rf_chamber_start(&s_config);
  if (error != ESP_OK) {
    show_error(error);
    return;
  }
  rf_chamber_snapshot(&status);
  s_owned_run = status.run_id;
  build(true);
}
static void input(const input_event_t *event, void *ctx) {
  (void)ctx;
  bool press = event->action == INPUT_ACTION_PRESS;
  if (rf_passive_is_boot()) {
    if (!press)
      return;
    rf_passive_status_t status = {0};
    rf_passive_snapshot(&status);
    if (event->button == INPUT_BTN_BACK)
      s_exit_passive = !s_exit_passive;
    else if (event->button == INPUT_BTN_OK && s_exit_passive) {
      esp_err_t error = rf_passive_save_boot(false, status.path);
      if (error == ESP_OK)
        esp_restart();
      else
        show_error(error);
    } else if (!s_exit_passive &&
               (event->button == INPUT_BTN_LEFT || event->button == INPUT_BTN_RIGHT ||
                event->button == INPUT_BTN_UP || event->button == INPUT_BTN_DOWN)) {
      int direction = event->button == INPUT_BTN_LEFT || event->button == INPUT_BTN_UP ? -1 : 1;
      esp_err_t error = rf_passive_select((status.path - 1 + 4 + direction) % 4 + 1);
      if (error != ESP_OK)
        show_error(error);
    }
    refresh(NULL);
    return;
  }
  bool nav = press || event->action == INPUT_ACTION_REPEAT;
  if (s_mesh_pending || s_is_leaving)
    return;
  if (s_is_live) {
    if (nav && event->button == INPUT_BTN_DOWN)
      lv_obj_scroll_by(s_body, 0, -40, LV_ANIM_OFF);
    if (nav && event->button == INPUT_BTN_UP)
      lv_obj_scroll_by(s_body, 0, 40, LV_ANIM_OFF);
    if (!press)
      return;
    rf_chamber_status_t status;
    rf_chamber_snapshot(&status);
    if (event->button == INPUT_BTN_BACK || event->button == INPUT_BTN_OK) {
      if (status.run_id == s_owned_run && status.phase != RF_CHAMBER_IDLE) {
        s_is_leaving = event->button == INPUT_BTN_BACK;
        stop_owned();
      } else if (event->button == INPUT_BTN_BACK)
        ui_switch_screen(s_return);
      else
        build(false);
    }
    return;
  }
  if (event->button == INPUT_BTN_BACK && press) {
    ui_switch_screen(s_return);
    return;
  }
  if (event->button == INPUT_BTN_UP && nav)
    menu_component_prev(&s_menu);
  if (event->button == INPUT_BTN_DOWN && nav)
    menu_component_next(&s_menu);
  int selected = menu_component_get_selected(&s_menu);
  if (selected < 0 || selected >= s_row_count)
    return;
  rf_lab_field_t field = s_rows[selected];
  if (event->button == INPUT_BTN_OK && press && field == FIELD_PASSIVE) {
    rf_chamber_status_t status;
    rf_chamber_snapshot(&status);
    if (status.phase != RF_CHAMBER_IDLE) {
      show_error(ESP_ERR_INVALID_STATE);
      return;
    }
    esp_err_t error = rf_passive_save_boot(true, s_config.path ? s_config.path : 1);
    if (error == ESP_OK)
      esp_restart();
    else
      show_error(error);
    return;
  }
  if (event->button == INPUT_BTN_OK && press && field == FIELD_START) {
    start();
    return;
  }
  if (event->button == INPUT_BTN_OK && press && field == FIELD_MESH_STOP) {
    esp_err_t error = rf_chamber_mesh_stop_async();
    if (error == ESP_OK) {
      s_mesh_pending = true;
      menu_component_set_hint(&s_menu, "Stopping mesh...");
    } else
      show_error(error);
    return;
  }
  if ((nav && (event->button == INPUT_BTN_LEFT || event->button == INPUT_BTN_RIGHT)) ||
      (press && event->button == INPUT_BTN_OK)) {
    adjust(field, event->button == INPUT_BTN_LEFT ? -1 : 1, event->button == INPUT_BTN_OK);
    if (field == FIELD_RADIO || field == FIELD_QUICK) {
      build(false);
      for (unsigned i = 0; i < s_row_count; i++)
        if (s_rows[i] == field)
          menu_component_select(&s_menu, i);
    } else {
      char text[32];
      for (unsigned i = 0; i < s_row_count; ++i) {
        if (s_rows[i] == FIELD_MESH_STOP || s_rows[i] == FIELD_PASSIVE || s_rows[i] == FIELD_START)
          continue;
        value(s_rows[i], text, sizeof(text));
        menu_component_set_selector_value(&s_menu, i, text);
      }
    }
  }
}
static void refresh(lv_timer_t *timer) {
  (void)timer;
  if (rf_passive_is_boot()) {
    if (s_readout == NULL)
      return;
    rf_passive_status_t s = {0};
    rf_passive_snapshot(&s);
    char text[480];
    snprintf(text,
             sizeof(text),
             "%s\nRF%u -> %s\nV1(GPIO17)=%u  V2(GPIO18)=%u\nC5 reset: %s\nCC sleep: %s | LoRa "
             "sleep: %s\nNFC off: %s | RFID off: %s\n\n%s",
             s.error == ESP_OK ? "READY / TX DISABLED" : "NOT READY - CHECK ERRORS",
             s.path,
             BRANCHES[s.path - 1],
             (s.path - 1) >> 1,
             (s.path - 1) & 1,
             s.hardware.c5 == ESP_OK ? "OK" : "ERROR",
             s.hardware.cc == ESP_OK ? "OK" : "ERROR",
             s.hardware.sx == ESP_OK ? "OK" : "ERROR",
             s.hardware.nfc == ESP_OK ? "OK" : "ERROR",
             s.hardware.lf == ESP_OK ? "OK" : "ERROR",
             s_exit_passive ? "OK: exit passive and reboot\nBACK: remain passive"
                            : "L/R: change state and save\nBACK: exit options");
    lv_label_set_text(s_readout, text);
    return;
  }
  if (s_mesh_pending) {
    bool busy;
    esp_err_t error = rf_chamber_mesh_stop_status(&busy);
    if (!busy) {
      s_mesh_pending = false;
      if (error != ESP_OK)
        show_error(error);
      build(false);
    }
  }
  if (!s_is_live || s_readout == NULL)
    return;
  rf_chamber_status_t status;
  rf_chamber_snapshot(&status);
  if (status.run_id != s_owned_run) {
    lv_label_set_text(s_readout, "Session changed outside this screen.");
    return;
  }
  if (s_is_leaving && status.phase == RF_CHAMBER_IDLE) {
    ui_switch_screen(s_return);
    return;
  }
  if (status.phase == RF_CHAMBER_HELD)
    s_is_leaving = false;
  const char *phase = status.phase == RF_CHAMBER_IDLE      ? "OFF / FINISHED"
                      : status.phase == RF_CHAMBER_HELD    ? "OFF NOT CONFIRMED"
                      : status.phase == RF_CHAMBER_RUNNING ? "RUNNING"
                                                           : "START / STOP";
  char signal[100] = "RSSI / SNR: --";
  if (status.samples)
    snprintf(signal,
             sizeof(signal),
             "RSSI avg %.1f / min %.1f dBm\nmax %.1f dBm | last SNR %d%s",
             (double)status.rssi_sum_half_dbm / (2.0 * status.samples),
             status.rssi_min_half_dbm / 2.0,
             status.rssi_max_half_dbm / 2.0,
             status.last_snr_db,
             status.config.radio == RF_CHAMBER_SX && status.rx_test + status.rx_other > 0
                 ? " dB"
                 : " (no packet)");
  // LVGL's reduced printf has float formatting disabled in this profile.
  char display[640];
  snprintf(display,
           sizeof(display),
           "%s  #%lu\n%lu.%06lu MHz | %+d dBm set\n%s | RF%u | %.1f / %.1f s\nTX done %lu / queued "
           "%lu\nRX test %lu / other %lu / err %lu\n%s\nWait %lu | Timeout %lu\nRun: %s\nStop: %s",
           phase,
           (unsigned long)status.run_id,
           (unsigned long)(status.config.frequency_hz / MHZ),
           (unsigned long)(status.config.frequency_hz % MHZ),
           status.config.power_dbm,
           MODES[status.config.mode],
           status.config.path,
           status.elapsed_ms / 1000.0,
           status.config.duration_ms / 1000.0,
           (unsigned long)status.tx_done,
           (unsigned long)status.tx_submitted,
           (unsigned long)status.rx_test,
           (unsigned long)status.rx_other,
           (unsigned long)status.rx_error,
           signal,
           (unsigned long)status.duty_deferred,
           (unsigned long)status.tx_timeout,
           esp_err_to_name(status.error),
           esp_err_to_name(status.cleanup_error));
  lv_label_set_text(s_readout, display);
}
static void build(bool live) {
  lv_obj_t *outgoing = s_screen;
  s_is_live = live;
  s_readout = s_body = NULL;
  s_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(s_screen, current_theme.screen_base, 0);
  lv_obj_set_style_pad_all(s_screen, 0, 0);
  lv_obj_set_style_border_width(s_screen, 0, 0);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(s_screen, deleted, LV_EVENT_DELETE, NULL);
  if (!live && !rf_passive_is_boot()) {
    s_menu = menu_component_create(s_screen, "RF LAB", "/assets/icons/tune.bin");
    s_row_count = 0;
    for (unsigned order = 0; order < FIELD_COUNT; ++order) {
      rf_lab_field_t field = order == 0   ? FIELD_PASSIVE
                             : order == 1 ? FIELD_QUICK
                                          : (rf_lab_field_t)(order - 2);
      if ((field == FIELD_PATH && s_config.radio == RF_CHAMBER_SX) ||
          (field >= FIELD_SF && field <= FIELD_CR && s_config.radio == RF_CHAMBER_CC))
        continue;
      s_rows[s_row_count++] = field;
      char text[32];
      value(field, text, sizeof(text));
      if (field == FIELD_START || field == FIELD_MESH_STOP || field == FIELD_PASSIVE)
        menu_component_add_item(&s_menu,
                                "/assets/icons/tune.bin",
                                field == FIELD_START     ? "Start test"
                                : field == FIELD_PASSIVE ? "Switch + tags / restart"
                                                         : "Stop mesh session");
      else
        menu_component_add_selector(&s_menu, NULL, NAMES[field], text);
    }
    menu_component_set_hint(&s_menu, "UP/DN select  L/R edit  OK apply");
  } else {
    ui_chrome_header(s_screen,
                     rf_passive_is_boot() ? "PASSIVE RF SWITCH" : "RF LAB / LIVE",
                     "/assets/icons/tune.bin");
    s_body = lv_obj_create(s_screen);
    lv_obj_set_pos(s_body, BODY_MARGIN, BODY_TOP);
    lv_obj_set_size(s_body, ui_screen_w() - 2 * BODY_MARGIN, ui_screen_h() - BODY_TOP - 28);
    lv_obj_set_style_bg_opa(s_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_body, 0, 0);
    lv_obj_set_style_pad_all(s_body, 0, 0);
    lv_obj_set_scroll_dir(s_body, LV_DIR_VER);
    s_readout = lv_label_create(s_body);
    lv_obj_set_width(s_readout, ui_screen_w() - 2 * BODY_MARGIN - 4);
    lv_obj_set_style_text_color(s_readout, current_theme.text_main, 0);
    lv_obj_set_style_text_font(s_readout, &lv_font_montserrat_12, 0);
    lv_label_set_text(s_readout, "Starting...");
    ui_chrome_footer(s_screen,
                     rf_passive_is_boot() ? "L/R state  BACK exit options"
                                          : "OK stop / settings  BACK stop / exit");
    if (rf_passive_is_boot())
      power_policy_set_display_hold(s_screen, true);
  }
  ui_input_set_screen_handler(input, NULL);
  ui_screen_load_owned(&s_screen, s_screen);
  if (outgoing != NULL && outgoing != s_screen && ui_current_screen() == SCREEN_LORA_RNODE)
    lv_obj_del_async(outgoing);
}
void ui_rf_lab_from_subghz(bool from_subghz) {
  s_from_subghz = from_subghz;
}
void ui_lora_rnode_open(void) {
  if (rf_passive_is_boot()) {
    ui_rf_bench_open();
    return;
  }
  // ui_switch_screen owns the outgoing screen, including orientation rebuilds.
  s_screen = s_readout = s_body = NULL;
  s_return = s_from_subghz ? SCREEN_SUBGHZ_MENU : SCREEN_MENU;
  s_config.radio = s_from_subghz ? RF_CHAMBER_CC : RF_CHAMBER_SX;
  s_config.frequency_hz = s_config.radio == RF_CHAMBER_CC ? FREQUENCIES[1] : FREQUENCIES[2];
  s_config.power_dbm = s_config.radio == RF_CHAMBER_CC ? -30 : -9;
  s_config.path = s_config.radio == RF_CHAMBER_CC ? 1 : 0;
  s_config.sf = 7;
  s_config.bandwidth_khz = 125;
  s_config.cr = 1;
  s_config.mode = RF_CHAMBER_RX;
  s_config.duration_ms = 5000;
  s_owned_run = 0;
  s_is_leaving = s_mesh_pending = s_exit_passive = false;
  s_preset = 0;
  build(false);
  s_timer = lv_timer_create(refresh, REFRESH_MS, NULL);
  if (s_timer == NULL) {
    show_error(ESP_ERR_NO_MEM);
  }
}
