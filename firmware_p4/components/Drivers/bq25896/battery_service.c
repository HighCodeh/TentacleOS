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

static battery_snapshot_t s_snap; // guarded by s_mtx once the task is running
static SemaphoreHandle_t s_mtx = NULL;
static bool s_started = false;

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
    if (!t.adc_continuous) {
      ESP_LOGW(TAG, "charger ADC stopped; restoring continuous conversion");
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

  // One synchronous read so the first UI paint already has real data.
  bq25896_telem_t t;
  if (bq25896_read_telemetry(&t) == ESP_OK && t.adc_continuous) {
    battery_service_filter_t filter = {0};
    s_snap.soc = filter_soc(&filter, &t);
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
