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

#include "lf_rfid.h"

#include <limits.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_def.h"
#include "rf_frontend.h"

#define VOLTAGE_MIN_MS 100U
#define VOLTAGE_MAX_MS 1000U
#define VOLTAGE_BATCH  32U
#define SETTLE_MS      450U
#define EXTERNAL_MS    30U
#define US_PER_MS      1000U

esp_err_t
lf_rfid_measure_voltage(bool reader, uint32_t duration_ms, lf_rfid_voltage_report_t *out) {
  if (out == NULL || duration_ms < VOLTAGE_MIN_MS || duration_ms > VOLTAGE_MAX_MS)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  esp_err_t error = rf_frontend_reserve(RF_FRONTEND_LF_125KHZ);
  if (error != ESP_OK)
    return error;

  adc_oneshot_unit_handle_t adc = NULL;
  adc_cali_handle_t calibration = NULL;
  bool pin_changed = false;
  adc_unit_t unit;
  adc_channel_t channel;
  uint64_t total = 0;
  int minimum = INT_MAX;
  int maximum = 0;
  error = lf_rfid_init();
  if (error != ESP_OK)
    goto cleanup;
  error = lf_rfid_field_stop_checked();
  if (error != ESP_OK)
    goto cleanup;
  if (reader) {
    lf_rfid_field_report_t external = {0};
    error = lf_rfid_listen_external(EXTERNAL_MS, &external);
    if (error == ESP_OK && external.carrier_present)
      error = ESP_ERR_INVALID_STATE;
    if (error != ESP_OK)
      goto cleanup;
    error = lf_rfid_field_start(LF_RFID_NOMINAL_HZ, LF_RFID_DEFAULT_DUTY_PERMILLE);
    if (error != ESP_OK)
      goto cleanup;
    vTaskDelay(pdMS_TO_TICKS(SETTLE_MS));
  }
  error = adc_oneshot_io_to_channel(GPIO_RFID_LF_DATA_PIN, &unit, &channel);
  if (error != ESP_OK)
    goto cleanup;
  const adc_oneshot_unit_init_cfg_t init = {.unit_id = unit};
  error = adc_oneshot_new_unit(&init, &adc);
  if (error != ESP_OK)
    goto cleanup;
  const adc_oneshot_chan_cfg_t config = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_DEFAULT,
  };
  pin_changed = true;
  error = adc_oneshot_config_channel(adc, channel, &config);
  if (error != ESP_OK)
    goto cleanup;
  const adc_cali_curve_fitting_config_t cal_config = {
      .unit_id = unit,
      .chan = channel,
      .atten = config.atten,
      .bitwidth = config.bitwidth,
  };
  error = adc_cali_create_scheme_curve_fitting(&cal_config, &calibration);
  if (error != ESP_OK && error != ESP_ERR_NOT_SUPPORTED)
    goto cleanup;
  error = ESP_OK;
  const int64_t deadline = esp_timer_get_time() + (int64_t)duration_ms * US_PER_MS;
  while (esp_timer_get_time() < deadline) {
    int raw = 0;
    error = adc_oneshot_read(adc, channel, &raw);
    if (error != ESP_OK)
      goto cleanup;
    if (raw < minimum)
      minimum = raw;
    if (raw > maximum)
      maximum = raw;
    total += raw;
    out->samples++;
    if (out->samples % VOLTAGE_BATCH == 0)
      vTaskDelay(1);
  }
  if (out->samples == 0) {
    error = ESP_ERR_NOT_FOUND;
    goto cleanup;
  }
  out->raw_min = minimum;
  out->raw_max = maximum;
  out->raw_mean = total / out->samples;
  if (calibration != NULL) {
    error = adc_cali_raw_to_voltage(calibration, minimum, &out->min_mv);
    if (error == ESP_OK)
      error = adc_cali_raw_to_voltage(calibration, maximum, &out->max_mv);
    out->calibrated = error == ESP_OK;
  }

cleanup:;
  // Stop the RF output even if ADC cleanup fails. Do not hand a partially
  // restored pin back to a digital reader or an NFC worker.
  esp_err_t stopped = lf_rfid_field_stop_checked();
  esp_err_t released = ESP_OK;
  if (calibration != NULL)
    released = adc_cali_delete_scheme_curve_fitting(calibration);
  if (adc != NULL) {
    esp_err_t deleted = adc_oneshot_del_unit(adc);
    if (released == ESP_OK)
      released = deleted;
  }
  if (pin_changed) {
    const gpio_config_t input = {
        .pin_bit_mask = 1ULL << GPIO_RFID_LF_DATA_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t restored = gpio_config(&input);
    if (released == ESP_OK)
      released = restored;
  }
  if (stopped != ESP_OK || released != ESP_OK) {
    out->requires_reset = true;
    return stopped != ESP_OK ? stopped : released;
  }
  released = rf_frontend_unreserve(RF_FRONTEND_LF_125KHZ);
  if (released != ESP_OK) {
    out->requires_reset = true;
    return released;
  }
  return error;
}
