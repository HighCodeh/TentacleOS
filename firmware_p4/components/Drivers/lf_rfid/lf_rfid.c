// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lf_rfid.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pin_def.h"
#include "rf_frontend.h"

static const char *TAG = "LF_RFID";

#define LF_LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LF_LEDC_TIMER          LEDC_TIMER_3
#define LF_LEDC_CHANNEL        LEDC_CHANNEL_7
#define LF_LEDC_RESOLUTION     LEDC_TIMER_8_BIT
#define LF_LEDC_MAX_DUTY       255U
#define LF_MEASURE_MIN_MS      10U
#define LF_MEASURE_MAX_MS      1000U
#define LF_SAMPLE_US           64U
#define LF_SAMPLES_PER_BIT     8U
#define LF_FRAME_BITS          64U
#define LF_FRAME_SAMPLES       (LF_FRAME_BITS * LF_SAMPLES_PER_BIT)
#define LF_SAMPLE_MAX          4096U
#define LF_SETTLE_MS           5U
#define LF_PCNT_CHUNK_MS       100U
#define LF_PCNT_GLITCH_NS      250U
#define LF_EXTERNAL_CHECK_MS   30U
#define LF_MOD_RATE_MIN_HZ     100U
#define LF_MOD_RATE_MAX_HZ     10000U
#define LF_MOD_DURATION_MIN_MS 20U
#define LF_MOD_DURATION_MAX_MS 2000U

typedef struct {
  volatile uint32_t data_edges;
  volatile int64_t last_data_edge_us;
  lf_rfid_edge_t *volatile capture;
  volatile size_t capture_capacity;
  volatile size_t capture_count;
} lf_capture_state_t;

static bool s_initialized;
static bool s_field_on;
static uint32_t s_requested_hz = LF_RFID_NOMINAL_HZ;
static uint32_t s_generated_hz;
static lf_capture_state_t s_capture;
static pcnt_unit_handle_t s_carrier_counter;
static pcnt_channel_handle_t s_carrier_channel;

static void reset_measurement_state(void) {
  memset(&s_capture, 0, sizeof(s_capture));
}

static void IRAM_ATTR data_isr(void *arg) {
  (void)arg;
  const int64_t now = esp_timer_get_time();
  s_capture.data_edges++;

  if (s_capture.capture != NULL && s_capture.capture_count < s_capture.capture_capacity &&
      s_capture.last_data_edge_us != 0) {
    const size_t index = s_capture.capture_count++;
    s_capture.capture[index].delta_us = (uint32_t)(now - s_capture.last_data_edge_us);
    s_capture.capture[index].level = (uint8_t)gpio_get_level(GPIO_RFID_LF_DATA_PIN);
  }
  s_capture.last_data_edge_us = now;
}

static esp_err_t set_measurement_interrupts(bool enable_carrier, bool enable_data) {
  esp_err_t err = ESP_OK;
  if (enable_carrier) {
    err = gpio_set_intr_type(GPIO_RFID_LF_CARRIER_PIN, GPIO_INTR_ANYEDGE);
    if (err == ESP_OK) {
      err = gpio_intr_enable(GPIO_RFID_LF_CARRIER_PIN);
    }
  } else {
    gpio_intr_disable(GPIO_RFID_LF_CARRIER_PIN);
  }
  if (err != ESP_OK) {
    return err;
  }

  if (enable_data) {
    err = gpio_set_intr_type(GPIO_RFID_LF_DATA_PIN, GPIO_INTR_ANYEDGE);
    if (err == ESP_OK) {
      err = gpio_intr_enable(GPIO_RFID_LF_DATA_PIN);
    }
  } else {
    gpio_intr_disable(GPIO_RFID_LF_DATA_PIN);
  }
  return err;
}

esp_err_t lf_rfid_init(void) {
  if (!rf_frontend_can_access(RF_FRONTEND_LF_125KHZ)) {
    return ESP_ERR_INVALID_STATE;
  }
  if (s_initialized) {
    return ESP_OK;
  }

  /* Safe state first. Si2302CDS replacements are active-high N-MOSFETs. */
  gpio_config_t output_cfg = {
      .pin_bit_mask = (1ULL << GPIO_RFID_LF_COIL_PIN) | (1ULL << GPIO_RFID_LF_MOD_PIN),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_ENABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_RETURN_ON_ERROR(gpio_config(&output_cfg), TAG, "LF output GPIO config failed");
  gpio_set_level(GPIO_RFID_LF_MOD_PIN, 0);
  gpio_set_level(GPIO_RFID_LF_COIL_PIN, 0);

  gpio_config_t input_cfg = {
      .pin_bit_mask = (1ULL << GPIO_RFID_LF_CARRIER_PIN) | (1ULL << GPIO_RFID_LF_DATA_PIN),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  ESP_RETURN_ON_ERROR(gpio_config(&input_cfg), TAG, "LF input GPIO config failed");

  pcnt_unit_config_t counter_cfg = {
      .low_limit = -1,
      .high_limit = 30000,
  };
  ESP_RETURN_ON_ERROR(
      pcnt_new_unit(&counter_cfg, &s_carrier_counter), TAG, "LF PCNT unit create failed");
  pcnt_glitch_filter_config_t filter_cfg = {
      .max_glitch_ns = LF_PCNT_GLITCH_NS,
  };
  ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(s_carrier_counter, &filter_cfg),
                      TAG,
                      "LF PCNT filter config failed");
  pcnt_chan_config_t counter_channel_cfg = {
      .edge_gpio_num = GPIO_RFID_LF_CARRIER_PIN,
      .level_gpio_num = -1,
  };
  ESP_RETURN_ON_ERROR(pcnt_new_channel(s_carrier_counter, &counter_channel_cfg, &s_carrier_channel),
                      TAG,
                      "LF PCNT channel create failed");
  ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(s_carrier_channel,
                                                   PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                                   PCNT_CHANNEL_EDGE_ACTION_HOLD),
                      TAG,
                      "LF PCNT edge action failed");
  ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(s_carrier_channel,
                                                    PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                                    PCNT_CHANNEL_LEVEL_ACTION_KEEP),
                      TAG,
                      "LF PCNT level action failed");
  ESP_RETURN_ON_ERROR(pcnt_unit_enable(s_carrier_counter), TAG, "LF PCNT unit enable failed");

  ledc_timer_config_t timer = {
      .speed_mode = LF_LEDC_MODE,
      .duty_resolution = LF_LEDC_RESOLUTION,
      .timer_num = LF_LEDC_TIMER,
      .freq_hz = LF_RFID_NOMINAL_HZ,
      /* ESP32-P4 has one global LEDC clock for all low-speed timers. Use the
       * 80 MHz PLL source also selected by the 5 kHz/13-bit display backlight,
       * otherwise AUTO picks XTAL here and the display timer cannot start. */
      .clk_cfg = LEDC_USE_PLL_DIV_CLK,
  };
  ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "LF LEDC timer config failed");

  ledc_channel_config_t channel = {
      .gpio_num = GPIO_RFID_LF_MOD_PIN,
      .speed_mode = LF_LEDC_MODE,
      .channel = LF_LEDC_CHANNEL,
      .intr_type = LEDC_INTR_DISABLE,
      .timer_sel = LF_LEDC_TIMER,
      .duty = 0,
      .hpoint = 0,
      .flags.output_invert = 0,
  };
  ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "LF LEDC channel config failed");

  esp_err_t err = gpio_isr_handler_add(GPIO_RFID_LF_DATA_PIN, data_isr, NULL);
  if (err != ESP_OK) {
    return err;
  }
  set_measurement_interrupts(false, false);

  s_generated_hz = ledc_get_freq(LF_LEDC_MODE, LF_LEDC_TIMER);
  s_initialized = true;
  ESP_LOGI(TAG,
           "Ready: EN=GPIO%d MOD=GPIO%d CL=GPIO%d DA=GPIO%d (field OFF)",
           GPIO_RFID_LF_COIL_PIN,
           GPIO_RFID_LF_MOD_PIN,
           GPIO_RFID_LF_CARRIER_PIN,
           GPIO_RFID_LF_DATA_PIN);
  return ESP_OK;
}

void lf_rfid_deinit(void) {
  if (!rf_frontend_can_access(RF_FRONTEND_LF_125KHZ)) {
    return;
  }
  if (!s_initialized) {
    return;
  }
  lf_rfid_field_stop();
  set_measurement_interrupts(false, false);
  gpio_isr_handler_remove(GPIO_RFID_LF_DATA_PIN);
  if (s_carrier_counter != NULL) {
    pcnt_unit_stop(s_carrier_counter);
    pcnt_unit_disable(s_carrier_counter);
  }
  if (s_carrier_channel != NULL) {
    pcnt_del_channel(s_carrier_channel);
    s_carrier_channel = NULL;
  }
  if (s_carrier_counter != NULL) {
    pcnt_del_unit(s_carrier_counter);
    s_carrier_counter = NULL;
  }
  s_initialized = false;
}

esp_err_t lf_rfid_field_start(uint32_t frequency_hz, uint16_t duty_permille) {
  if (!rf_frontend_can_access(RF_FRONTEND_LF_125KHZ)) {
    return ESP_ERR_INVALID_STATE;
  }
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  if (frequency_hz < LF_RFID_MIN_HZ || frequency_hz > LF_RFID_MAX_HZ || duty_permille < 100 ||
      duty_permille > 900) {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_field_on) {
    return (frequency_hz == s_requested_hz) ? ESP_OK : ESP_ERR_INVALID_STATE;
  }

  ESP_RETURN_ON_ERROR(rf_frontend_claim(RF_FRONTEND_LF_125KHZ), TAG, "antenna is busy");
  esp_err_t freq_err = ledc_set_freq(LF_LEDC_MODE, LF_LEDC_TIMER, frequency_hz);
  if (freq_err != ESP_OK) {
    rf_frontend_release(RF_FRONTEND_LF_125KHZ);
    return freq_err;
  }
  uint32_t actual = ledc_get_freq(LF_LEDC_MODE, LF_LEDC_TIMER);
  if (actual == 0) {
    rf_frontend_release(RF_FRONTEND_LF_125KHZ);
    return ESP_FAIL;
  }

  const uint32_t duty = (LF_LEDC_MAX_DUTY * duty_permille + 500U) / 1000U;
  gpio_set_level(GPIO_RFID_LF_COIL_PIN, 1);
  esp_rom_delay_us(100);
  esp_err_t err = ledc_set_duty(LF_LEDC_MODE, LF_LEDC_CHANNEL, duty);
  if (err == ESP_OK) {
    err = ledc_update_duty(LF_LEDC_MODE, LF_LEDC_CHANNEL);
  }
  if (err != ESP_OK) {
    gpio_set_level(GPIO_RFID_LF_COIL_PIN, 0);
    rf_frontend_release(RF_FRONTEND_LF_125KHZ);
    return err;
  }

  s_requested_hz = frequency_hz;
  s_generated_hz = actual;
  s_field_on = true;
  return ESP_OK;
}

void lf_rfid_field_stop(void) {
  if (!rf_frontend_can_access(RF_FRONTEND_LF_125KHZ)) {
    return;
  }
  if (!s_initialized) {
    return;
  }
  const bool was_on = s_field_on;
  set_measurement_interrupts(false, false);
  ledc_set_duty(LF_LEDC_MODE, LF_LEDC_CHANNEL, 0);
  ledc_update_duty(LF_LEDC_MODE, LF_LEDC_CHANNEL);
  gpio_set_level(GPIO_RFID_LF_COIL_PIN, 0);
  s_field_on = false;
  if (was_on)
    rf_frontend_release(RF_FRONTEND_LF_125KHZ);
}

bool lf_rfid_field_is_on(void) {
  return s_field_on;
}

uint32_t lf_rfid_field_frequency(void) {
  return s_generated_hz;
}

static esp_err_t measure_feedback(uint32_t window_ms,
                                  uint32_t reference_hz,
                                  uint32_t generated_hz,
                                  bool field_active,
                                  lf_rfid_field_report_t *out) {
  if (out == NULL || window_ms < LF_MEASURE_MIN_MS || window_ms > LF_MEASURE_MAX_MS) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }

  memset(out, 0, sizeof(*out));
  reset_measurement_state();
  esp_err_t err = set_measurement_interrupts(false, true);
  if (err != ESP_OK) {
    return err;
  }

  uint32_t remaining_ms = window_ms;
  uint64_t elapsed_us = 0;
  uint64_t carrier_pulses = 0;
  bool counter_running = false;
  while (remaining_ms > 0) {
    const uint32_t chunk_ms = remaining_ms > LF_PCNT_CHUNK_MS ? LF_PCNT_CHUNK_MS : remaining_ms;
    err = pcnt_unit_clear_count(s_carrier_counter);
    if (err != ESP_OK) {
      break;
    }
    const int64_t started_us = esp_timer_get_time();
    err = pcnt_unit_start(s_carrier_counter);
    if (err != ESP_OK) {
      break;
    }
    counter_running = true;
    vTaskDelay(pdMS_TO_TICKS(chunk_ms));
    err = pcnt_unit_stop(s_carrier_counter);
    counter_running = false;
    if (err != ESP_OK) {
      break;
    }
    const int64_t stopped_us = esp_timer_get_time();
    int chunk_pulses = 0;
    err = pcnt_unit_get_count(s_carrier_counter, &chunk_pulses);
    if (err != ESP_OK) {
      break;
    }
    if (chunk_pulses > 0) {
      carrier_pulses += (uint32_t)chunk_pulses;
    }
    elapsed_us += (uint64_t)(stopped_us - started_us);
    remaining_ms -= chunk_ms;
  }
  if (counter_running) {
    (void)pcnt_unit_stop(s_carrier_counter);
  }
  set_measurement_interrupts(false, false);
  if (err != ESP_OK) {
    return err;
  }

  out->requested_hz = reference_hz;
  out->generated_hz = generated_hz;
  out->window_ms = window_ms;
  out->carrier_edges = (uint32_t)carrier_pulses;
  out->data_edges = s_capture.data_edges;
  out->data_edge_rate_hz = (uint32_t)(((uint64_t)out->data_edges * 1000U) / window_ms);
  out->field_active = field_active;

  if (carrier_pulses >= 2 && elapsed_us > 0) {
    out->measured_hz = (uint32_t)((carrier_pulses * 1000000ULL) / elapsed_us);
    out->carrier_present = out->measured_hz >= LF_RFID_MIN_HZ && out->measured_hz <= LF_RFID_MAX_HZ;
    out->frequency_error_ppm = (int32_t)(((int64_t)out->measured_hz - (int64_t)reference_hz) *
                                         1000000LL / (int64_t)reference_hz);

    int quality = 100;
    int64_t abs_error = out->frequency_error_ppm;
    if (abs_error < 0) {
      abs_error = -abs_error;
    }
    quality -= (int)((abs_error / 1000) > 45 ? 45 : (abs_error / 1000));
    if (quality < 0) {
      quality = 0;
    }
    out->quality_percent = (uint8_t)quality;
  }
  return ESP_OK;
}

esp_err_t lf_rfid_measure(uint32_t window_ms, lf_rfid_field_report_t *out) {
  if (!rf_frontend_can_access(RF_FRONTEND_LF_125KHZ)) {
    return ESP_ERR_INVALID_STATE;
  }
  if (!s_initialized || !s_field_on) {
    return ESP_ERR_INVALID_STATE;
  }
  return measure_feedback(window_ms, s_requested_hz, s_generated_hz, true, out);
}

esp_err_t lf_rfid_listen_external(uint32_t window_ms, lf_rfid_field_report_t *out) {
  if (!rf_frontend_can_access(RF_FRONTEND_LF_125KHZ)) {
    return ESP_ERR_INVALID_STATE;
  }
  if (out == NULL || window_ms < LF_MEASURE_MIN_MS || window_ms > LF_MEASURE_MAX_MS) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_initialized || s_field_on || rf_frontend_get_mode() != RF_FRONTEND_IDLE) {
    return ESP_ERR_INVALID_STATE;
  }

  /* A passive listen must never energize T3/T4/T5 or the T2 modulation path. */
  (void)ledc_set_duty(LF_LEDC_MODE, LF_LEDC_CHANNEL, 0);
  (void)ledc_update_duty(LF_LEDC_MODE, LF_LEDC_CHANNEL);
  gpio_set_level(GPIO_RFID_LF_MOD_PIN, 0);
  gpio_set_level(GPIO_RFID_LF_COIL_PIN, 0);

  esp_err_t err = rf_frontend_claim(RF_FRONTEND_LF_125KHZ);
  if (err == ESP_OK) {
    err = measure_feedback(window_ms, LF_RFID_NOMINAL_HZ, 0, false, out);
    rf_frontend_release(RF_FRONTEND_LF_125KHZ);
  }
  return err;
}

esp_err_t lf_rfid_modulation_test(uint32_t rate_hz,
                                  uint32_t duration_ms,
                                  lf_rfid_field_report_t *external_field) {
  if (!rf_frontend_can_access(RF_FRONTEND_LF_125KHZ)) {
    return ESP_ERR_INVALID_STATE;
  }
  if (external_field == NULL || rate_hz < LF_MOD_RATE_MIN_HZ || rate_hz > LF_MOD_RATE_MAX_HZ ||
      duration_ms < LF_MOD_DURATION_MIN_MS || duration_ms > LF_MOD_DURATION_MAX_MS) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_initialized || s_field_on || rf_frontend_get_mode() != RF_FRONTEND_IDLE) {
    return ESP_ERR_INVALID_STATE;
  }

  gpio_set_level(GPIO_RFID_LF_COIL_PIN, 0);
  (void)ledc_set_duty(LF_LEDC_MODE, LF_LEDC_CHANNEL, 0);
  (void)ledc_update_duty(LF_LEDC_MODE, LF_LEDC_CHANNEL);

  esp_err_t err = rf_frontend_claim(RF_FRONTEND_LF_125KHZ);
  if (err != ESP_OK) {
    return err;
  }

  err = measure_feedback(LF_EXTERNAL_CHECK_MS, LF_RFID_NOMINAL_HZ, 0, false, external_field);
  if (err != ESP_OK || !external_field->carrier_present) {
    err = err == ESP_OK ? ESP_ERR_NOT_FOUND : err;
    goto cleanup;
  }

  err = ledc_set_freq(LF_LEDC_MODE, LF_LEDC_TIMER, rate_hz);
  if (err != ESP_OK) {
    goto cleanup;
  }
  err = ledc_set_duty(LF_LEDC_MODE, LF_LEDC_CHANNEL, LF_LEDC_MAX_DUTY / 2U);
  if (err == ESP_OK) {
    err = ledc_update_duty(LF_LEDC_MODE, LF_LEDC_CHANNEL);
  }
  if (err == ESP_OK) {
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
  }

cleanup:
  (void)ledc_set_duty(LF_LEDC_MODE, LF_LEDC_CHANNEL, 0);
  (void)ledc_update_duty(LF_LEDC_MODE, LF_LEDC_CHANNEL);
  gpio_set_level(GPIO_RFID_LF_MOD_PIN, 0);
  gpio_set_level(GPIO_RFID_LF_COIL_PIN, 0);
  (void)ledc_set_freq(LF_LEDC_MODE, LF_LEDC_TIMER, LF_RFID_NOMINAL_HZ);
  s_requested_hz = LF_RFID_NOMINAL_HZ;
  s_generated_hz = ledc_get_freq(LF_LEDC_MODE, LF_LEDC_TIMER);
  rf_frontend_release(RF_FRONTEND_LF_125KHZ);
  return err;
}

esp_err_t lf_rfid_capture_edges(uint32_t duration_ms,
                                lf_rfid_edge_t *edges,
                                size_t capacity,
                                size_t *out_count) {
  if (!rf_frontend_can_access(RF_FRONTEND_LF_125KHZ)) {
    return ESP_ERR_INVALID_STATE;
  }
  if (edges == NULL || out_count == NULL || capacity == 0 || capacity > LF_RFID_RAW_MAX_EDGES ||
      duration_ms == 0 || duration_ms > LF_MEASURE_MAX_MS) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_initialized || !s_field_on) {
    return ESP_ERR_INVALID_STATE;
  }

  reset_measurement_state();
  s_capture.capture = edges;
  s_capture.capture_capacity = capacity;
  ESP_RETURN_ON_ERROR(set_measurement_interrupts(false, true), TAG, "data capture enable failed");
  vTaskDelay(pdMS_TO_TICKS(duration_ms));
  set_measurement_interrupts(false, false);
  *out_count = s_capture.capture_count;
  s_capture.capture = NULL;
  s_capture.capture_capacity = 0;
  return (*out_count != 0) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static bool em4100_validate(const uint8_t bits[LF_FRAME_BITS], lf_rfid_em4100_t *out) {
  for (size_t i = 0; i < 9; i++) {
    if (bits[i] != 1) {
      return false;
    }
  }
  if (bits[63] != 0) {
    return false;
  }

  for (size_t row = 0; row < 10; row++) {
    uint8_t parity = 0;
    for (size_t col = 0; col < 4; col++) {
      parity ^= bits[9 + row * 5 + col];
    }
    if (parity != bits[9 + row * 5 + 4]) {
      return false;
    }
  }
  for (size_t col = 0; col < 4; col++) {
    uint8_t parity = 0;
    for (size_t row = 0; row < 10; row++) {
      parity ^= bits[9 + row * 5 + col];
    }
    if (parity != bits[59 + col]) {
      return false;
    }
  }

  memset(out->bytes, 0, sizeof(out->bytes));
  for (size_t row = 0; row < 10; row++) {
    uint8_t nibble = 0;
    for (size_t col = 0; col < 4; col++) {
      nibble = (uint8_t)((nibble << 1) | bits[9 + row * 5 + col]);
    }
    if ((row & 1U) == 0) {
      out->bytes[row / 2] = (uint8_t)(nibble << 4);
    } else {
      out->bytes[row / 2] |= nibble;
    }
  }
  out->id = 0;
  for (size_t i = 0; i < sizeof(out->bytes); i++) {
    out->id = (out->id << 8) | out->bytes[i];
  }
  return true;
}

static bool em4100_decode_samples(const uint8_t *samples, size_t count, lf_rfid_em4100_t *out) {
  uint8_t bits[LF_FRAME_BITS];
  if (count < LF_FRAME_SAMPLES) {
    return false;
  }

  for (size_t start = 0; start + LF_FRAME_SAMPLES <= count; start++) {
    for (unsigned polarity = 0; polarity < 2; polarity++) {
      bool valid_manchester = true;
      for (size_t bit = 0; bit < LF_FRAME_BITS; bit++) {
        unsigned first_high = 0;
        unsigned second_high = 0;
        size_t base = start + bit * LF_SAMPLES_PER_BIT;
        for (size_t i = 0; i < LF_SAMPLES_PER_BIT / 2; i++) {
          first_high += samples[base + i] != 0;
          second_high += samples[base + LF_SAMPLES_PER_BIT / 2 + i] != 0;
        }
        uint8_t first = first_high >= (LF_SAMPLES_PER_BIT / 4);
        uint8_t second = second_high >= (LF_SAMPLES_PER_BIT / 4);
        if (first == second) {
          valid_manchester = false;
          break;
        }
        bits[bit] = (uint8_t)(second ^ polarity);
      }
      if (valid_manchester && em4100_validate(bits, out)) {
        out->inverted = polarity != 0;
        out->samples_used = LF_FRAME_SAMPLES;
        return true;
      }
    }
  }
  return false;
}

esp_err_t lf_rfid_read_em4100(uint32_t timeout_ms, lf_rfid_em4100_t *out) {
  if (out == NULL || timeout_ms < 40 || timeout_ms > 1000) {
    return ESP_ERR_INVALID_ARG;
  }
  if (!s_initialized || !s_field_on) {
    return ESP_ERR_INVALID_STATE;
  }

  size_t sample_count = ((size_t)timeout_ms * 1000U) / LF_SAMPLE_US;
  if (sample_count > LF_SAMPLE_MAX) {
    sample_count = LF_SAMPLE_MAX;
  }
  if (sample_count < LF_FRAME_SAMPLES) {
    sample_count = LF_FRAME_SAMPLES;
  }
  uint8_t *samples = malloc(sample_count);
  if (samples == NULL) {
    return ESP_ERR_NO_MEM;
  }

  vTaskDelay(pdMS_TO_TICKS(LF_SETTLE_MS));
  int64_t next_sample = esp_timer_get_time();
  for (size_t i = 0; i < sample_count; i++) {
    next_sample += LF_SAMPLE_US;
    int64_t remaining = next_sample - esp_timer_get_time();
    if (remaining > 0) {
      esp_rom_delay_us((uint32_t)remaining);
    }
    samples[i] = (uint8_t)gpio_get_level(GPIO_RFID_LF_DATA_PIN);
  }

  memset(out, 0, sizeof(*out));
  bool found = em4100_decode_samples(samples, sample_count, out);
  free(samples);
  return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t lf_rfid_self_test(lf_rfid_self_test_t *out) {
  if (!rf_frontend_can_access(RF_FRONTEND_LF_125KHZ)) {
    return ESP_ERR_INVALID_STATE;
  }
  if (out == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  memset(out, 0, sizeof(*out));
  ESP_RETURN_ON_ERROR(lf_rfid_init(), TAG, "init failed");

  lf_rfid_field_stop();
  out->gpio_static_ok = gpio_get_level(GPIO_RFID_LF_COIL_PIN) == 0;
  lf_rfid_field_report_t external = {0};
  esp_err_t err = lf_rfid_listen_external(LF_EXTERNAL_CHECK_MS, &external);
  if (err != ESP_OK) {
    return err;
  }
  if (external.carrier_present) {
    ESP_LOGW(TAG, "External 125 kHz field detected; active self-test refused");
    return ESP_ERR_INVALID_STATE;
  }
  err = lf_rfid_field_start(LF_RFID_NOMINAL_HZ, LF_RFID_DEFAULT_DUTY_PERMILLE);
  if (err != ESP_OK) {
    return err;
  }
  vTaskDelay(pdMS_TO_TICKS(LF_SETTLE_MS));
  err = lf_rfid_measure(20, &out->field);
  lf_rfid_field_stop();
  if (err != ESP_OK) {
    return err;
  }

  out->carrier_present = out->field.carrier_present;
  int64_t error = out->field.frequency_error_ppm;
  if (error < 0) {
    error = -error;
  }
  out->frequency_ok = error <= 50000; /* +/-5%, generous for first hardware bring-up. */
  return (out->gpio_static_ok && out->carrier_present && out->frequency_ok)
             ? ESP_OK
             : ESP_ERR_INVALID_RESPONSE;
}
