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

#include "battery_service.h"

#include <stdatomic.h>
#include <string.h>
#include "bq27220.h"
#include "power_profile.h"
#include "pin_def.h"
#include "led_control.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "esp_log.h"

static const char *TAG = "BATTERY_SVC";

#define POLL_INTERVAL_MS 1000
#define CHG_OFF_DEBOUNCE 3 // not-charging polls (while still plugged) before dropping the flag
#define TASK_STACK       4096
#define SOC_STEP_MAX     1
#define SOC_UPDATE_POLLS 30
#define SOC_SETTLE_POLLS 30
#define SOC_FILTER_SCALE 256
#define SOC_MAX_PARTIAL  99
#define SOC_FULL_PERCENT 100
#define SOC_EMA_DEN      4  // EMA: ema = (ema*3 + raw)/4 (rejects load sag)
#define LOW_ENTER_PCT    15 // latch "low" at/below this SoC
#define LOW_EXIT_PCT     20 // release "low" at/above this SoC (hysteresis)
#define LOW_ENTER_MV     3300
#define LOW_EXIT_MV      3500
#define PACK_NOMINAL_MV  3700
#define PACK_604858_RECOVERY_MA 20

typedef struct {
  int soc;
  int ema_scaled;
  int anchor_soc;
  int anchor_raw;
  unsigned stable_polls;
  unsigned settle_polls;
  bool has_sample;
  bool on_usb;
  bool charging;
} battery_service_filter_t;

static battery_snapshot_t s_snap; // guarded by s_mtx once the task is running
static SemaphoreHandle_t s_mtx = NULL;
static bool s_started = false;
static battery_charge_snapshot_t s_charge;
static bool s_charge_requested = true;
static SemaphoreHandle_t s_charge_control;
static const battery_service_profile_t BATTERY_PROFILES[] = {
    {BATTERY_SERVICE_DEFAULT_PROFILE,
     "Generic",
     POWER_PROFILE_CAPACITY_MAH,
     PACK_NOMINAL_MV,
     0,
     POWER_PROFILE_CHARGE_GUARDED},
    {"generic_2000",
     "Generic",
     POWER_PROFILE_CAPACITY_SMALL_MAH,
     PACK_NOMINAL_MV,
     0,
     POWER_PROFILE_CHARGE_GUARDED},
    {"knlypow_604858",
     "KNLYPOW 604858",
     POWER_PROFILE_CAPACITY_SMALL_MAH,
     PACK_NOMINAL_MV,
     PACK_604858_RECOVERY_MA,
     POWER_PROFILE_CHARGE_604858},
};
#define BATTERY_PROFILES_COUNT (sizeof(BATTERY_PROFILES) / sizeof(BATTERY_PROFILES[0]))
static atomic_uint_least32_t s_profile_index;

static int filter_soc(battery_service_filter_t *filter, const bq25896_telem_t *sample);

const battery_service_profile_t *battery_service_profile_at(uint32_t index) {
  return index < BATTERY_PROFILES_COUNT ? &BATTERY_PROFILES[index] : NULL;
}

const battery_service_profile_t *battery_service_profile_find(const char *id) {
  if (id == NULL)
    return NULL;
  for (uint32_t index = 0; index < BATTERY_PROFILES_COUNT; ++index) {
    if (strcmp(id, BATTERY_PROFILES[index].id) == 0)
      return &BATTERY_PROFILES[index];
  }
  return NULL;
}

const battery_service_profile_t *battery_service_profile_get(void) {
  return &BATTERY_PROFILES[atomic_load_explicit(&s_profile_index, memory_order_relaxed)];
}

esp_err_t battery_service_set_profile(const char *id) {
  const battery_service_profile_t *profile = battery_service_profile_find(id);
  if (profile == NULL)
    return ESP_ERR_INVALID_ARG;
  atomic_store_explicit(
      &s_profile_index, (uint32_t)(profile - BATTERY_PROFILES), memory_order_relaxed);
  return ESP_OK;
}

uint16_t battery_service_capacity_mah(void) {
  return battery_service_profile_get()->capacity_mah;
}

bool battery_service_get_charge(battery_charge_snapshot_t *out) {
  if (out == NULL || s_mtx == NULL || xSemaphoreTake(s_mtx, pdMS_TO_TICKS(50)) != pdTRUE)
    return false;
  *out = s_charge;
  xSemaphoreGive(s_mtx);
  return out->valid;
}

esp_err_t battery_service_request_charge(bool enable) {
  if (!s_started || s_mtx == NULL)
    return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(50)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  s_charge_requested = enable;
  s_charge.requested = enable;
  xSemaphoreGive(s_mtx);
  return ESP_OK;
}

#ifdef CONFIG_HIGHBOY_BOARD_V203
static const char *charge_block_reason(const bq25896_telem_t *t, power_profile_charge_t profile) {
  if (!t->adc_valid)
    return "Battery reading unavailable";
  if (!t->power_good)
    return "Connect USB to charge";
  if (t->vbat_mv <= 2304)
    return "Battery not detected";
  if (t->is_recovery_required || t->vbat_mv <= POWER_PROFILE_CUTOFF_MV)
    return "Low cell: recovery unsupported";
  uint16_t minimum_mv = power_profile_charge_min_mv(profile);
  if (minimum_mv == 0)
    return "Battery profile invalid";
  if (t->vbat_mv < minimum_mv)
    return "Battery voltage too low";
  if (t->vbat_mv > 4200)
    return "Battery voltage too high";
  if (t->fault_live)
    return "Charger fault";
  if (t->ts_milli_pct < 50000 || t->ts_milli_pct > 65000)
    return "Temperature input outside range";
  if (t->otg_enabled)
    return "Disable USB power output";
  if (t->charge_current_limit_ma > 384 || t->charge_voltage_limit_mv > 4176 || t->ichg_ma > 500)
    return "Charge limits invalid";
  return NULL;
}

static void battery_pack_task(void *arg) {
  (void)arg;
  bool low = false;
  battery_service_filter_t voltage_filter = {0};
  const char *last_status = NULL;
  for (;;) {
    xSemaphoreTake(s_charge_control, portMAX_DELAY);
    bool requested;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    requested = s_charge_requested;
    xSemaphoreGive(s_mtx);
    battery_charge_snapshot_t next = {
        .requested = requested, .soc = -1, .status = "Charger unavailable"};
    esp_err_t error = bq25896_read_telemetry(&next.charger);
    if (error == ESP_OK) {
      next.valid = true;
      power_profile_charge_t profile = battery_service_profile_get()->charge_profile;
      const char *blocked = charge_block_reason(&next.charger, profile);
      bool desired = requested && blocked == NULL;
      if (next.charger.charge_enabled != desired) {
        error = bq25896_set_charge_enable_for_profile(desired, profile);
        if (error == ESP_OK)
          error = bq25896_read_telemetry(&next.charger);
        if (error == ESP_OK) {
          blocked = charge_block_reason(&next.charger, profile);
          if (blocked != NULL && next.charger.charge_enabled) {
            error = bq25896_stop_charge();
            if (error == ESP_OK)
              error = bq25896_read_telemetry(&next.charger);
          }
        }
      }
      if (error != ESP_OK) {
        (void)bq25896_stop_charge();
        next.valid = false;
        next.status = "Charge control failed";
      } else if (blocked != NULL)
        next.status = blocked;
      else if (!requested)
        next.status = "Charging paused";
      else if (next.charger.chg == CHARGE_STATUS_CHARGE_DONE)
        next.status = "Charge complete";
      else if (next.charger.charging)
        next.status = "Charging";
      else
        next.status = "Charge enabled - waiting";
      bq27220_sample_t gauge;
      if (bq27220_read_sample(&gauge) == ESP_OK && gauge.voltage_mv > 2304 &&
          gauge.voltage_mv <= 4250) {
        next.current_valid = true;
        next.battery_current_ma = gauge.current_ma;
        if (POWER_PROFILE_GAUGE_QUALIFIED && gauge.soc_pct <= 100 && gauge.voltage_mv >= 3000 &&
            gauge.design_mah == battery_service_capacity_mah() && gauge.full_mah > 0 &&
            gauge.remaining_mah <= gauge.full_mah) {
          next.gauge_valid = true;
          next.soc = gauge.soc_pct;
        }
      }
    } else {
      (void)bq25896_stop_charge();
    }
    bool voltage_valid = next.valid && next.charger.adc_valid && next.charger.vbat_mv > 2304;
    if (!voltage_valid) {
      next.soc = -1;
      if (next.valid && next.charger.adc_valid)
        voltage_filter = (battery_service_filter_t){0};
    } else if (!next.gauge_valid) {
      next.soc = filter_soc(&voltage_filter, &next.charger);
    }
    if (!voltage_valid || next.charger.power_good)
      low = false;
    else if (next.gauge_valid ? next.soc <= LOW_ENTER_PCT : next.charger.vbat_mv <= LOW_ENTER_MV)
      low = true;
    else if (next.gauge_valid ? next.soc >= LOW_EXIT_PCT : next.charger.vbat_mv >= LOW_EXIT_MV)
      low = false;
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_charge = next;
    s_snap = (battery_snapshot_t){.soc = next.soc,
                                  .is_soc_estimated = voltage_valid && !next.gauge_valid,
                                  .vbat_mv = next.charger.vbat_mv,
                                  .present = voltage_valid,
                                  .charging = next.valid && next.charger.charging,
                                  .vbus_present = next.valid && next.charger.power_good,
                                  .low = low,
                                  .valid = next.valid && next.charger.adc_valid,
                                  .chg = next.charger.chg};
    xSemaphoreGive(s_mtx);
    xSemaphoreGive(s_charge_control);
    led_power_state_t led_state = LED_POWER_OFF;
    if (next.valid && next.charger.power_good) {
      if (next.charger.charging)
        led_state = LED_POWER_CHARGING;
      else if (next.charger.chg == CHARGE_STATUS_CHARGE_DONE)
        led_state = LED_POWER_FULL;
      else if (next.requested)
        led_state = LED_POWER_BLOCKED;
    } else if (low)
      led_state = LED_POWER_LOW;
    led_set_power_state(led_state);
    if (last_status != next.status) {
      if (led_state == LED_POWER_BLOCKED || !next.valid) {
        ESP_LOGW(TAG, "%s", next.status);
        led_signal_warning();
      } else {
        ESP_LOGI(TAG, "%s", next.status);
        led_signal_info();
      }
      last_status = next.status;
    }
    vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
  }
}
#endif

static int clamp_pct(int v) {
  if (v < 0)
    return 0;
  if (v > 100)
    return 100;
  return v;
}

static int step_toward(int prev, int target) {
  int d = target - prev;
  if (d > SOC_STEP_MAX)
    d = SOC_STEP_MAX;
  else if (d < -SOC_STEP_MAX)
    d = -SOC_STEP_MAX;
  return prev + d;
}

static int filter_soc(battery_service_filter_t *filter, const bq25896_telem_t *sample) {
  int raw = clamp_pct(sample->soc);
  if (sample->chg != CHARGE_STATUS_CHARGE_DONE && raw > SOC_MAX_PARTIAL)
    raw = SOC_MAX_PARTIAL;
  if (!filter->has_sample) {
    filter->soc = raw;
    filter->ema_scaled = raw * SOC_FILTER_SCALE;
    filter->anchor_soc = raw;
    filter->anchor_raw = raw;
    filter->has_sample = true;
    filter->on_usb = sample->power_good;
    filter->charging = sample->charging;
    return filter->soc;
  }
  if (filter->on_usb != sample->power_good || filter->charging != sample->charging) {
    filter->on_usb = sample->power_good;
    filter->charging = sample->charging;
    filter->settle_polls = SOC_SETTLE_POLLS;
    filter->stable_polls = 0;
    filter->ema_scaled = raw * SOC_FILTER_SCALE;
  }
  if (filter->settle_polls != 0) {
    filter->ema_scaled += (raw * SOC_FILTER_SCALE - filter->ema_scaled) / SOC_EMA_DEN;
    if (--filter->settle_polls == 0) {
      // Preserve charge across the power-source voltage step, not just its display delay.
      filter->anchor_soc = filter->soc;
      filter->anchor_raw = (filter->ema_scaled + SOC_FILTER_SCALE / 2) / SOC_FILTER_SCALE;
    }
    return filter->soc;
  }
  filter->ema_scaled += (raw * SOC_FILTER_SCALE - filter->ema_scaled) / SOC_EMA_DEN;
  if (++filter->stable_polls < SOC_UPDATE_POLLS)
    return filter->soc;
  filter->stable_polls = 0;
  int filtered = (filter->ema_scaled + SOC_FILTER_SCALE / 2) / SOC_FILTER_SCALE;
  int target = filter->anchor_soc;
  if (filtered < filter->anchor_raw && filter->anchor_raw > 0) {
    target = (filtered * filter->anchor_soc + filter->anchor_raw / 2) / filter->anchor_raw;
  } else if (filtered > filter->anchor_raw && filter->anchor_raw < SOC_FULL_PERCENT) {
    int range = SOC_FULL_PERCENT - filter->anchor_raw;
    target +=
        ((filtered - filter->anchor_raw) * (SOC_FULL_PERCENT - filter->anchor_soc) + range / 2) /
        range;
  }
  if (target > SOC_MAX_PARTIAL && filter->soc < SOC_FULL_PERCENT)
    target = SOC_MAX_PARTIAL;
  if (sample->power_good && sample->chg == CHARGE_STATUS_CHARGE_DONE)
    target = SOC_FULL_PERCENT;
  else if (!sample->charging && target > filter->soc)
    target = filter->soc;
  filter->soc = step_toward(filter->soc, target);
  return filter->soc;
}

static void battery_task(void *arg) {
  (void)arg;
  int soc = s_snap.soc;
  battery_service_filter_t filter = {
      .soc = s_snap.soc,
      .ema_scaled = s_snap.soc * SOC_FILTER_SCALE,
      .anchor_soc = s_snap.soc,
      .anchor_raw = s_snap.soc,
      .has_sample = s_snap.valid,
      .on_usb = s_snap.vbus_present,
      .charging = s_snap.charging,
  };
  bool low = s_snap.low;
  bool charging_disp = s_snap.charging; // debounced charging shown to the UI
  int chg_off = 0;
  int read_failures = 0;
  bool first = true;

  for (;;) {
    // Allow the first continuous ADC conversion to finish.
    vTaskDelay(pdMS_TO_TICKS(first ? 1200 : POLL_INTERVAL_MS));
    first = false;

    bq25896_telem_t t;
    esp_err_t read_err = bq25896_read_telemetry(&t);
    if (read_err != ESP_OK) {
      if (++read_failures >= 3) {
        ESP_LOGW(TAG,
                 "charger telemetry failed 3x (%s); reapplying runtime config",
                 esp_err_to_name(read_err));
        (void)bq25896_reapply_runtime_config();
        read_failures = 0;
      }
      continue;
    }
    read_failures = 0;

    // A charger watchdog/POR would clear continuous conversion. Repair it
    // before accepting default 2.304-V ADC codes as real battery data.
    if (!t.adc_valid) {
      ESP_LOGW(TAG, "charger ADC invalid; checking runtime configuration");
      (void)bq25896_reapply_runtime_config();
      continue;
    }

    soc = filter_soc(&filter, &t);

    // Low-battery hysteresis: never low while charging or on external power;
    // otherwise latch below LOW_ENTER_PCT, release at/above LOW_EXIT_PCT.
    if (t.charging || t.power_good)
      low = false;
    else if (soc <= LOW_ENTER_PCT || t.soc <= LOW_ENTER_PCT)
      low = true;
    else if (soc >= LOW_EXIT_PCT && t.soc >= LOW_EXIT_PCT)
      low = false;

    // Debounce charging so the icon/bolt doesn't flicker when the charger
    // cycles (e.g. pre-charge on a marginal cell): turn ON immediately; turn
    // OFF immediately when unplugged; but require a few consecutive
    // not-charging polls to drop it while still on external power.
    if (t.charging) {
      charging_disp = true;
      chg_off = 0;
    } else if (!t.power_good) {
      charging_disp = false;
      chg_off = 0;
    } else if (++chg_off >= CHG_OFF_DEBOUNCE) {
      charging_disp = false;
    }

    if (xSemaphoreTake(s_mtx, portMAX_DELAY) == pdTRUE) {
      s_snap.soc = soc;
      s_snap.is_soc_estimated = true;
      s_snap.vbat_mv = t.vbat_mv;
      s_snap.present = bq25896_is_present();
      s_snap.charging = charging_disp;
      s_snap.vbus_present = t.power_good;
      s_snap.low = low;
      s_snap.chg = t.chg;
      s_snap.valid = true;
      xSemaphoreGive(s_mtx);
    }
  }
}

void battery_service_init(void) {
  if (s_started)
    return;

  s_mtx = xSemaphoreCreateMutex();
  if (s_mtx == NULL) {
    ESP_LOGE(TAG, "mutex alloc failed");
    return;
  }

#ifdef CONFIG_HIGHBOY_BOARD_V203
  s_charge_control = xSemaphoreCreateMutex();
  if (s_charge_control == NULL) {
    vSemaphoreDelete(s_mtx);
    s_mtx = NULL;
    return;
  }
  if (xTaskCreatePinnedToCore(battery_pack_task,
                              "battery_svc",
                              TASK_STACK,
                              NULL,
                              SYS_PRIO_BACKGROUND,
                              NULL,
                              SYS_CORE_RADIO) != pdPASS) {
    vSemaphoreDelete(s_charge_control);
    s_charge_control = NULL;
    vSemaphoreDelete(s_mtx);
    s_mtx = NULL;
    ESP_LOGE(TAG, "battery task create failed");
    return;
  }
  s_started = true;
  ESP_LOGI(TAG, "Guarded charge service: <=384 mA / 4176 mV; board NTC is not cell temperature");
  return;
#endif

  // One synchronous read so the first UI paint already has real data.
  bq25896_telem_t t;
  if (bq25896_read_telemetry(&t) == ESP_OK && t.adc_valid) {
    battery_service_filter_t filter = {0};
    s_snap.soc = filter_soc(&filter, &t);
    s_snap.is_soc_estimated = true;
    s_snap.vbat_mv = t.vbat_mv;
    s_snap.present = bq25896_is_present();
    s_snap.charging = t.charging;
    s_snap.vbus_present = t.power_good;
    s_snap.low = (!t.charging && !t.power_good && s_snap.soc <= LOW_ENTER_PCT);
    s_snap.chg = t.chg;
    s_snap.valid = true;
  }

  if (xTaskCreatePinnedToCore(battery_task,
                              "battery_svc",
                              TASK_STACK,
                              NULL,
                              SYS_PRIO_BACKGROUND,
                              NULL,
                              SYS_CORE_RADIO) != pdPASS) {
    ESP_LOGE(TAG, "task create failed");
    vSemaphoreDelete(s_mtx);
    s_mtx = NULL;
    return;
  }

  s_started = true;
  ESP_LOGI(TAG, "battery service started (soc=%d%%, charging=%d)", s_snap.soc, s_snap.charging);
}

bool battery_service_get(battery_snapshot_t *out) {
  if (out == NULL || s_mtx == NULL)
    return false;

  bool valid = false;
  if (xSemaphoreTake(s_mtx, pdMS_TO_TICKS(50)) == pdTRUE) {
    *out = s_snap;
    valid = s_snap.valid;
    xSemaphoreGive(s_mtx);
  }
  return valid;
}

int battery_service_soc(void) {
  battery_snapshot_t s;
  return battery_service_get(&s) ? s.soc : -1;
}

bool battery_service_is_low(void) {
  battery_snapshot_t s;
  return battery_service_get(&s) ? s.low : false;
}
