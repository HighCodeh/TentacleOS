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

#include "power_policy.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include "audio_i2s.h"
#include "battery_service.h"
#include "bq25896.h"
#include "input_manager.h"
#include "led_control.h"
#include "lf_rfid.h"
#include "msgbox_ui.h"
#include "notify_ui.h"
#include "power_manager.h"
#include "st7789.h"
#include "storage_init.h"
#include "tos_config.h"
#include "ui_manager.h"
#include "ui_theme.h"

// Battery discharge policy, manual shutdown handling, and display inactivity.

#define POWER_POLL_MS          100
#define BATT_CHECK_MS          2000
#define BACK_POWEROFF_HOLD_MS  5000
#define COMBO_POWEROFF_HOLD_MS 3000
#define DISPLAY_OFF_DELAY_MS   1600
#define DISPLAY_DIM_LEAD_MS    5000
#define DISPLAY_MIN_DIM_PCT    5
#define DISPLAY_DIM_DIVISOR    5

#define BATT_CRIT_PCT         5
#define BATT_SHUTDOWN_PCT     2
#define BATT_SHUTDOWN_SAMPLES 3

static const char *TAG = "POWER_POLICY";

static lv_timer_t *s_timer = NULL;
static bool s_low_last = false;
static bool s_crit_last = false;
static int s_shutdown_count = 0;
static uint32_t s_batt_accum_ms = 0;

static bool s_back_tracking = false;
static bool s_back_fired = false;
static uint32_t s_back_pressed_at = 0;
static uint32_t s_combo_held_ms = 0;
static bool s_combo_fired = false;
static bool s_shutdown_started = false;
static bool s_display_dimmed = false;
static bool s_display_asleep = false;
static bool s_wake_guard = false;
static uint32_t s_state_activity_ms = 0;
static const lv_obj_t *s_display_hold_owner = NULL;

static void display_hold_owner_deleted_cb(lv_event_t *event) {
  if (lv_event_get_target_obj(event) == s_display_hold_owner)
    s_display_hold_owner = NULL;
}

void power_policy_set_display_hold(const lv_obj_t *owner, bool enabled) {
  if (owner == NULL)
    return;
  if (!enabled) {
    if (s_display_hold_owner != owner)
      return;
    s_display_hold_owner = NULL;
    lv_obj_remove_event_cb((lv_obj_t *)owner, display_hold_owner_deleted_cb);
    return;
  }
  if (s_shutdown_started || s_display_hold_owner == owner || !lv_obj_is_valid(owner))
    return;
  if (s_display_hold_owner != NULL)
    lv_obj_remove_event_cb((lv_obj_t *)s_display_hold_owner, display_hold_owner_deleted_cb);
  s_display_hold_owner = NULL;
  if (lv_obj_add_event_cb(
          (lv_obj_t *)owner, display_hold_owner_deleted_cb, LV_EVENT_DELETE, NULL) != NULL)
    s_display_hold_owner = owner;
}

static uint32_t monotonic_ms(void) {
  return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool any_button_down(void) {
  for (input_button_t button = 0; button < INPUT_BTN_COUNT; button++)
    if (input_is_down(button))
      return true;
  return false;
}

static uint8_t configured_brightness(void) {
  int brightness = g_config_screen.brightness;
  if (brightness < 20)
    brightness = 20;
  if (brightness > 100)
    brightness = 100;
  return (uint8_t)brightness;
}

static void display_wake(void) {
  if (s_display_asleep)
    lcd_display_sleep(false);
  lcd_apply_brightness(configured_brightness());
  s_display_asleep = false;
  s_display_dimmed = false;
  s_wake_guard = true;
  ESP_LOGI(TAG, "display awake");
}

static void display_dim(uint32_t activity_ms) {
  uint8_t dim = configured_brightness() / DISPLAY_DIM_DIVISOR;
  if (dim < DISPLAY_MIN_DIM_PCT)
    dim = DISPLAY_MIN_DIM_PCT;
  lcd_apply_brightness(dim);
  s_display_dimmed = true;
  s_state_activity_ms = activity_ms;
  ESP_LOGI(TAG, "display dimmed to %u%%", dim);
}

static void display_sleep(uint32_t activity_ms) {
  lcd_apply_brightness(0);
  lcd_display_sleep(true);
  s_display_asleep = true;
  s_display_dimmed = false;
  s_state_activity_ms = activity_ms;
  ESP_LOGI(TAG, "display asleep");
}

static void check_display_inactivity(void) {
  uint32_t activity_ms = input_last_activity_ms();

  if ((s_display_asleep || s_display_dimmed) && activity_ms != s_state_activity_ms) {
    display_wake();
    return;
  }

  if (s_wake_guard) {
    if (!any_button_down())
      s_wake_guard = false;
    return;
  }

  // A running visible animation can hold just the display. Inactive screens,
  // pause and deletion immediately restore the configured inactivity policy.
  // Keep the wake guard above this branch: waking must not swallow input for
  // the lifetime of a hold. Battery and manual OFF checks remain in tick_cb.
  if (s_display_hold_owner != NULL && s_display_hold_owner == lv_screen_active()) {
    if (s_display_asleep || s_display_dimmed)
      display_wake();
    return;
  }

  int timeout_seconds = g_config_screen.auto_lock_seconds;
  if (timeout_seconds <= 0) {
    if (s_display_asleep || s_display_dimmed)
      display_wake();
    return;
  }

  uint32_t timeout_ms = (uint32_t)timeout_seconds * 1000U;
  uint32_t idle_ms = monotonic_ms() - activity_ms;
  if (!s_display_asleep && idle_ms >= timeout_ms) {
    display_sleep(activity_ms);
    return;
  }

  if (g_config_screen.auto_dim && !s_display_dimmed) {
    uint32_t dim_at_ms =
        timeout_ms > DISPLAY_DIM_LEAD_MS ? timeout_ms - DISPLAY_DIM_LEAD_MS : timeout_ms / 2U;
    if (idle_ms >= dim_at_ms)
      display_dim(activity_ms);
  }
}

static void shutdown_display_off_cb(lv_timer_t *timer) {
  lv_timer_delete(timer);
  lcd_apply_brightness(0);
  lcd_display_sleep(true);
}

static void show_shutdown_screen(bool usb_present, const char *reason) {
  lv_obj_t *outgoing = lv_screen_active();
  lv_obj_t *screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = lv_label_create(screen);
  lv_label_set_text(title, LV_SYMBOL_POWER "  Desligando...");
  lv_obj_set_style_text_color(title, current_theme.text_main, 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -28);

  lv_obj_t *detail = lv_label_create(screen);
  if (usb_present) {
    lv_label_set_text(detail, "Estado salvo com seguranca.\nDesconecte o USB para concluir.");
  } else if (reason != NULL) {
    lv_label_set_text_fmt(detail, "%s\nCortando a alimentacao...", reason);
  } else {
    lv_label_set_text(detail, "Estado salvo com seguranca.\nCortando a alimentacao...");
  }
  lv_obj_set_width(detail, lv_pct(86));
  lv_label_set_long_mode(detail, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(detail, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(detail, lv_color_hex(0x8A8594), 0);
  lv_obj_set_style_text_font(detail, &lv_font_montserrat_12, 0);
  lv_obj_align(detail, LV_ALIGN_CENTER, 0, 20);

  ui_screen_load(screen);
  if (outgoing != NULL && outgoing != screen && lv_obj_is_valid(outgoing))
    lv_obj_del_async(outgoing);

  // With no VBUS the BQ25896 BATFET delay gives the message time to paint;
  // extinguish the panel early so the user gets an unambiguous OFF indication.
  if (!usb_present) {
    lv_timer_t *timer = lv_timer_create(shutdown_display_off_cb, DISPLAY_OFF_DELAY_MS, NULL);
    lv_timer_set_repeat_count(timer, 1);
  }
}

static bool external_power_present(void) {
  battery_snapshot_t snapshot;
  if (battery_service_get(&snapshot))
    return snapshot.vbus_present;

  bq25896_telem_t telemetry;
  if (bq25896_read_telemetry(&telemetry) == ESP_OK)
    return telemetry.vbus_good || telemetry.power_good;
  return false;
}

static void begin_shutdown(const char *reason) {
  if (s_shutdown_started)
    return;

  bool usb_present = external_power_present();

  // Arm the delayed BATFET cut before unmounting. If the I2C command fails,
  // leave the running system and its storage untouched.
  esp_err_t power_err = bq25896_power_off();
  if (power_err != ESP_OK) {
    ESP_LOGE(TAG, "ship-mode request failed: %s", esp_err_to_name(power_err));
    notify(NOTIFY_WARNING, "Falha ao desligar");
    return;
  }

  s_shutdown_started = true;
  ESP_LOGW(TAG,
           "shutdown armed (%s, VBUS=%s)",
           reason ? reason : "manual",
           usb_present ? "present" : "absent");

  // Stop operation-specific workers before their screen is destroyed, then
  // present a standalone screen that no longer accepts application input.
  ui_prepare_shutdown();
  show_shutdown_screen(usb_present, reason);

  // Quiesce the remaining user-visible outputs and RF field while the BQ25896
  // runs its delayed BATFET sequence.
  audio_i2s_set_volume(0);
  audio_i2s_stream_stop();
  audio_i2s_mic_stream_stop();
  lf_rfid_field_stop();
  led_clear();

  // Unmounting flushes pending filesystem state. On USB the device
  // intentionally remains on this final screen until the cable is removed.
  if (storage_is_mounted()) {
    esp_err_t storage_err = storage_deinit();
    if (storage_err != ESP_OK)
      ESP_LOGE(TAG, "storage flush/unmount failed: %s", esp_err_to_name(storage_err));
  }
}

static void shutdown_confirm_cb(bool confirm) {
  if (confirm)
    begin_shutdown("Desligamento manual");
}

void power_policy_show_shutdown_dialog(void) {
  if (s_shutdown_started || msgbox_is_open())
    return;

  // Destructive action: Cancel is deliberately selected first.
  msgbox_open_ex(LV_SYMBOL_POWER,
                 "Desligar o High Boy?\nOs dados serao salvos com seguranca.",
                 "Desligar",
                 "Cancelar",
                 shutdown_confirm_cb,
                 false);
}

static void check_battery(void) {
  battery_snapshot_t snapshot;
  if (!battery_service_get(&snapshot))
    return;

  power_manager_set_external_power(snapshot.vbus_present);
  bool on_battery = snapshot.present && !snapshot.charging && !snapshot.vbus_present;

  if (snapshot.low && !s_low_last) {
    notify(NOTIFY_WARNING, "Battery low");
    led_signal_warning();
  }
  s_low_last = snapshot.low;

  bool critical = on_battery && snapshot.soc <= BATT_CRIT_PCT;
  if (critical && !s_crit_last)
    notify(NOTIFY_WARNING, "Battery critical - charge now");
  s_crit_last = critical;

  if (on_battery && snapshot.soc <= BATT_SHUTDOWN_PCT) {
    if (++s_shutdown_count >= BATT_SHUTDOWN_SAMPLES) {
      s_shutdown_count = 0;
      begin_shutdown("Bateria esgotada");
    }
  } else {
    s_shutdown_count = 0;
  }
}

static void check_manual_shutdown_gestures(void) {
  const uint32_t now = lv_tick_get();
  const bool back_down = input_is_down(INPUT_BTN_BACK);

  if (!back_down) {
    s_back_tracking = false;
    s_back_fired = false;
  } else if (!s_back_tracking) {
    s_back_tracking = true;
    s_back_pressed_at = now;
  } else if (!s_back_fired && (uint32_t)(now - s_back_pressed_at) >= BACK_POWEROFF_HOLD_MS &&
             !msgbox_is_open()) {
    s_back_fired = true;
    power_policy_show_shutdown_dialog();
  }

  // Preserve the old OK+LEFT recovery shortcut, but route it through the same
  // safe confirmation instead of cutting power immediately.
  if (input_is_down(INPUT_BTN_OK) && input_is_down(INPUT_BTN_LEFT)) {
    if (!s_combo_fired)
      s_combo_held_ms += POWER_POLL_MS;
    if (!s_combo_fired && s_combo_held_ms >= COMBO_POWEROFF_HOLD_MS && !msgbox_is_open()) {
      s_combo_fired = true;
      power_policy_show_shutdown_dialog();
    }
  } else {
    s_combo_held_ms = 0;
    s_combo_fired = false;
  }
}

static void tick_cb(lv_timer_t *timer) {
  (void)timer;
  if (s_shutdown_started)
    return;

  check_manual_shutdown_gestures();
  check_display_inactivity();

  s_batt_accum_ms += POWER_POLL_MS;
  if (s_batt_accum_ms >= BATT_CHECK_MS) {
    s_batt_accum_ms = 0;
    check_battery();
  }
}

bool power_policy_is_asleep(void) {
  return s_display_asleep || s_display_dimmed || s_wake_guard;
}

void power_policy_init(void) {
  if (s_timer != NULL)
    return;
  s_state_activity_ms = input_last_activity_ms();
  s_timer = lv_timer_create(tick_cb, POWER_POLL_MS, NULL);
}
