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

#include "rfid_lab_io.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/mcpwm_cap.h"
#include "driver/pulse_cnt.h"
#include "driver/rmt_tx.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "soc/soc_caps.h"

#include "pin_def.h"
#include "rfid_lab_protocol.h"

static const char *TAG = "RFID_IO";
#define PWM_MODE LEDC_LOW_SPEED_MODE
#define PWM_TIMER LEDC_TIMER_0
#define PWM_DRIVE LEDC_CHANNEL_0
#define PWM_LOAD LEDC_CHANNEL_1
#define PWM_CLOCK_HZ 80000000U
#define PERCENT_SCALE 100U
#define DEFAULT_HZ 125000U
#define MAX_PULSE_HZ 10000U
#define MIN_CARRIER_HZ 20000U
#define MAX_CARRIER_HZ 200000U
#define MIN_DUTY 10U
#define MAX_DUTY 90U
#define PCNT_LIMIT 30000
#define PCNT_FILTER_NS 100U
#define CAPTURE_HZ 1000000U
#define CAPTURE_RING_COUNT 1024U
#define EMULATION_HALF_CYCLES 32U
#define EMULATION_ID UINT64_C(0x0102030405)
#define RMT_MEMORY_BLOCKS 2U
#define EXTERNAL_MIN_HZ 110000U
#define EXTERNAL_MAX_HZ 140000U

static pcnt_unit_handle_t s_cl_counter;
static pcnt_unit_handle_t s_da_counter;
static mcpwm_cap_timer_handle_t s_capture_timer;
static mcpwm_cap_channel_handle_t s_capture_channel;
static rmt_channel_handle_t s_tx_channel;
static rmt_encoder_handle_t s_tx_encoder;
static bool s_tx_enabled;
static bool s_capture_enabled;
static bool s_pwm_initialized;
static uint32_t s_capture_resolution;
static portMUX_TYPE s_capture_lock = portMUX_INITIALIZER_UNLOCKED;
// ISR-owned internal RAM; no PSRAM, allocation or logging in the capture callback.
static DRAM_ATTR rfid_lab_edge_t s_ring[CAPTURE_RING_COUNT];
static uint32_t s_write_index;
static uint32_t s_read_index;
static uint32_t s_capture_edges;
static uint32_t s_dropped;
static uint32_t s_previous_tick;
static bool s_has_previous;
static DRAM_ATTR rmt_symbol_word_t s_tx_symbols[RFID_LAB_FRAME_BITS];

static bool IRAM_ATTR on_capture(mcpwm_cap_channel_handle_t channel,
                                 const mcpwm_capture_event_data_t *event, void *context) {
  (void)channel;
  (void)context;
  portENTER_CRITICAL_ISR(&s_capture_lock);
  s_capture_edges++;
  if (s_has_previous) {
    if (s_write_index - s_read_index >= CAPTURE_RING_COUNT) s_dropped++;
    else {
      rfid_lab_edge_t *edge = &s_ring[s_write_index % CAPTURE_RING_COUNT];
      edge->duration_us = event->cap_value - s_previous_tick;
      edge->level = event->cap_edge == MCPWM_CAP_EDGE_NEG;
      s_write_index++;
    }
  }
  s_previous_tick = event->cap_value;
  s_has_previous = true;
  portEXIT_CRITICAL_ISR(&s_capture_lock);
  return false;
}

static esp_err_t make_counter(gpio_num_t pin, bool both_edges, pcnt_unit_handle_t *out) {
  pcnt_unit_config_t config = {.low_limit = -PCNT_LIMIT, .high_limit = PCNT_LIMIT,
    .flags.accum_count = true};
  ESP_RETURN_ON_ERROR(pcnt_new_unit(&config, out), TAG, "counter allocation");
  pcnt_chan_config_t channel_config = {.edge_gpio_num = pin, .level_gpio_num = -1};
  pcnt_channel_handle_t channel;
  ESP_RETURN_ON_ERROR(pcnt_new_channel(*out, &channel_config, &channel), TAG, "counter channel");
  ESP_RETURN_ON_ERROR(pcnt_channel_set_edge_action(channel, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
    both_edges ? PCNT_CHANNEL_EDGE_ACTION_INCREASE : PCNT_CHANNEL_EDGE_ACTION_HOLD), TAG, "counter edge");
  ESP_RETURN_ON_ERROR(pcnt_channel_set_level_action(channel, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
    PCNT_CHANNEL_LEVEL_ACTION_KEEP), TAG, "counter level");
  pcnt_glitch_filter_config_t filter = {.max_glitch_ns = PCNT_FILTER_NS};
  ESP_RETURN_ON_ERROR(pcnt_unit_set_glitch_filter(*out, &filter), TAG, "counter filter");
  ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(*out, PCNT_LIMIT), TAG, "counter overflow");
  ESP_RETURN_ON_ERROR(pcnt_unit_add_watch_point(*out, -PCNT_LIMIT), TAG, "counter underflow");
  ESP_RETURN_ON_ERROR(pcnt_unit_enable(*out), TAG, "counter enable");
  ESP_RETURN_ON_ERROR(pcnt_unit_clear_count(*out), TAG, "counter clear");
  return pcnt_unit_start(*out);
}

esp_err_t rfid_lab_io_off(void) {
  esp_err_t result = ESP_OK;
  if (s_pwm_initialized) {
    if (ledc_stop(PWM_MODE, PWM_DRIVE, 0) != ESP_OK) result = ESP_FAIL;
    if (ledc_stop(PWM_MODE, PWM_LOAD, 0) != ESP_OK) result = ESP_FAIL;
  }
  if (s_tx_enabled) {
    if (rmt_disable(s_tx_channel) == ESP_OK) s_tx_enabled = false;
    else result = ESP_FAIL;
  }
  if (s_tx_channel != NULL && !s_tx_enabled) {
    if (rmt_del_channel(s_tx_channel) == ESP_OK) s_tx_channel = NULL;
    else result = ESP_FAIL;
  }
  if (s_tx_encoder != NULL && s_tx_channel == NULL) {
    if (rmt_del_encoder(s_tx_encoder) == ESP_OK) s_tx_encoder = NULL;
    else result = ESP_FAIL;
  }
  if (gpio_set_level(GPIO_RFID_LF_COIL_PIN, 0) != ESP_OK) result = ESP_FAIL;
  if (gpio_set_level(GPIO_RFID_LF_MOD_PIN, 0) != ESP_OK) result = ESP_FAIL;
  if (gpio_set_direction(GPIO_RFID_LF_COIL_PIN, GPIO_MODE_INPUT_OUTPUT) != ESP_OK) result = ESP_FAIL;
  if (gpio_set_direction(GPIO_RFID_LF_MOD_PIN, GPIO_MODE_INPUT_OUTPUT) != ESP_OK) result = ESP_FAIL;
  return result;
}

esp_err_t rfid_lab_io_init(void) {
  ESP_RETURN_ON_ERROR(rfid_lab_io_off(), TAG, "boot OFF");
  gpio_config_t input = {.pin_bit_mask = (1ULL << GPIO_RFID_LF_CARRIER_PIN) |
    (1ULL << GPIO_RFID_LF_DATA_PIN), .mode = GPIO_MODE_INPUT};
  ESP_RETURN_ON_ERROR(gpio_config(&input), TAG, "receive pins");
  ledc_timer_config_t timer = {.speed_mode = PWM_MODE, .timer_num = PWM_TIMER,
    .duty_resolution = LEDC_TIMER_8_BIT, .freq_hz = DEFAULT_HZ, .clk_cfg = LEDC_USE_PLL_DIV_CLK};
  ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "PWM timer");
  ledc_channel_config_t channel = {.gpio_num = GPIO_RFID_LF_COIL_PIN, .speed_mode = PWM_MODE,
    .channel = PWM_DRIVE, .timer_sel = PWM_TIMER, .duty = 0};
  ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "carrier PWM");
  s_pwm_initialized = true;
  channel.gpio_num = GPIO_RFID_LF_MOD_PIN;
  channel.channel = PWM_LOAD;
  ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "load PWM");
  ESP_RETURN_ON_ERROR(rfid_lab_io_off(), TAG, "PWM idle");
  ESP_RETURN_ON_ERROR(make_counter(GPIO_RFID_LF_CARRIER_PIN, false, &s_cl_counter), TAG, "CL counter");
  ESP_RETURN_ON_ERROR(make_counter(GPIO_RFID_LF_DATA_PIN, true, &s_da_counter), TAG, "DA counter");
  mcpwm_capture_timer_config_t capture = {.group_id = 0,
    .clk_src = MCPWM_CAPTURE_CLK_SRC_DEFAULT, .resolution_hz = CAPTURE_HZ};
  ESP_RETURN_ON_ERROR(mcpwm_new_capture_timer(&capture, &s_capture_timer), TAG, "capture timer");
  ESP_RETURN_ON_ERROR(mcpwm_capture_timer_get_resolution(s_capture_timer, &s_capture_resolution), TAG, "capture resolution");
  ESP_RETURN_ON_FALSE(s_capture_resolution == CAPTURE_HZ, ESP_ERR_NOT_SUPPORTED, TAG, "need 1 us capture ticks");
  mcpwm_capture_channel_config_t cap_channel = {.gpio_num = GPIO_RFID_LF_DATA_PIN,
    .prescale = 1, .flags.pos_edge = true, .flags.neg_edge = true};
  ESP_RETURN_ON_ERROR(mcpwm_new_capture_channel(s_capture_timer, &cap_channel, &s_capture_channel), TAG, "capture channel");
  mcpwm_capture_event_callbacks_t callbacks = {.on_cap = on_capture};
  ESP_RETURN_ON_ERROR(mcpwm_capture_channel_register_event_callbacks(s_capture_channel, &callbacks, NULL), TAG, "capture callback");
  ESP_RETURN_ON_ERROR(mcpwm_capture_timer_enable(s_capture_timer), TAG, "capture enable");
  return mcpwm_capture_timer_start(s_capture_timer);
}

esp_err_t rfid_lab_io_pwm(uint32_t pin, uint32_t hz, uint32_t duty, uint32_t *out_hz) {
  if (out_hz == NULL || duty < MIN_DUTY || duty > MAX_DUTY ||
      (pin != GPIO_RFID_LF_COIL_PIN && pin != GPIO_RFID_LF_MOD_PIN) ||
      (pin == GPIO_RFID_LF_COIL_PIN && (hz < MIN_CARRIER_HZ || hz > MAX_CARRIER_HZ)) ||
      (pin == GPIO_RFID_LF_MOD_PIN && (hz == 0 || hz > MAX_PULSE_HZ))) return ESP_ERR_INVALID_ARG;
  ESP_RETURN_ON_ERROR(rfid_lab_io_off(), TAG, "OFF before PWM");
  uint32_t resolution = ledc_find_suitable_duty_resolution(PWM_CLOCK_HZ, hz);
  if (resolution == 0) return ESP_ERR_INVALID_ARG;
  ledc_timer_config_t timer = {.speed_mode = PWM_MODE, .timer_num = PWM_TIMER,
    .duty_resolution = resolution, .freq_hz = hz, .clk_cfg = LEDC_USE_PLL_DIV_CLK};
  ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "PWM frequency");
  ledc_channel_t channel = pin == GPIO_RFID_LF_COIL_PIN ? PWM_DRIVE : PWM_LOAD;
  ESP_RETURN_ON_ERROR(ledc_set_pin(pin, PWM_MODE, channel), TAG, "PWM pin");
  ESP_RETURN_ON_ERROR(ledc_set_duty(PWM_MODE, channel, ((1U << resolution) * duty) / PERCENT_SCALE), TAG, "PWM duty");
  ESP_RETURN_ON_ERROR(ledc_update_duty(PWM_MODE, channel), TAG, "PWM start");
  *out_hz = ledc_get_freq(PWM_MODE, PWM_TIMER);
  return ESP_OK;
}

esp_err_t rfid_lab_io_static(uint32_t pin, uint32_t level) {
  if ((pin != GPIO_RFID_LF_COIL_PIN && pin != GPIO_RFID_LF_MOD_PIN) || level > 1) return ESP_ERR_INVALID_ARG;
  ESP_RETURN_ON_ERROR(rfid_lab_io_off(), TAG, "OFF before static output");
  return gpio_set_level(pin, level);
}

esp_err_t rfid_lab_io_emulate(uint32_t carrier_hz, bool invert) {
  if (carrier_hz < EXTERNAL_MIN_HZ || carrier_hz > EXTERNAL_MAX_HZ) return ESP_ERR_INVALID_ARG;
  ESP_RETURN_ON_ERROR(rfid_lab_io_off(), TAG, "OFF before emulation");
  uint32_t half_us = (EMULATION_HALF_CYCLES * CAPTURE_HZ + carrier_hz / 2U) / carrier_hz;
  uint64_t frame = rfid_lab_protocol_encode(EMULATION_ID);
  for (size_t bit = 0; bit < RFID_LAB_FRAME_BITS; bit++) {
    bool level = ((frame >> (RFID_LAB_FRAME_BITS - 1U - bit)) & 1U) ^ invert;
    s_tx_symbols[bit] = (rmt_symbol_word_t){.level0 = level, .duration0 = half_us,
      .level1 = !level, .duration1 = half_us};
  }
  rmt_tx_channel_config_t channel = {.gpio_num = GPIO_RFID_LF_MOD_PIN,
    .clk_src = RMT_CLK_SRC_DEFAULT, .resolution_hz = CAPTURE_HZ,
    .mem_block_symbols = RMT_MEMORY_BLOCKS * SOC_RMT_MEM_WORDS_PER_CHANNEL, .trans_queue_depth = 1};
  ESP_RETURN_ON_ERROR(rmt_new_tx_channel(&channel, &s_tx_channel), TAG, "emulation channel");
  rmt_copy_encoder_config_t encoder = {};
  ESP_RETURN_ON_ERROR(rmt_new_copy_encoder(&encoder, &s_tx_encoder), TAG, "emulation encoder");
  ESP_RETURN_ON_ERROR(rmt_enable(s_tx_channel), TAG, "emulation enable");
  s_tx_enabled = true;
  // Loop is stopped by the event-loop deadline or the manual OFF command.
  rmt_transmit_config_t transmit = {.loop_count = -1, .flags.eot_level = 0};
  return rmt_transmit(s_tx_channel, s_tx_encoder, s_tx_symbols, sizeof(s_tx_symbols), &transmit);
}

esp_err_t rfid_lab_io_capture(bool enable) {
  if (s_capture_enabled) {
    ESP_RETURN_ON_ERROR(mcpwm_capture_channel_disable(s_capture_channel), TAG, "capture stop");
    s_capture_enabled = false;
  }
  if (!enable) return ESP_OK;
  portENTER_CRITICAL(&s_capture_lock);
  s_write_index = 0;
  s_read_index = 0;
  s_capture_edges = 0;
  s_dropped = 0;
  s_has_previous = false;
  portEXIT_CRITICAL(&s_capture_lock);
  ESP_RETURN_ON_ERROR(mcpwm_capture_channel_enable(s_capture_channel), TAG, "capture start");
  s_capture_enabled = true;
  return ESP_OK;
}

esp_err_t rfid_lab_io_feedback(rfid_lab_feedback_t *out) {
  if (out == NULL) return ESP_ERR_INVALID_ARG;
  int cl_count;
  int da_count;
  ESP_RETURN_ON_ERROR(pcnt_unit_get_count(s_cl_counter, &cl_count), TAG, "CL count");
  ESP_RETURN_ON_ERROR(pcnt_unit_get_count(s_da_counter, &da_count), TAG, "DA count");
  out->time_us = esp_timer_get_time();
  out->cl_rises = (uint32_t)cl_count;
  out->da_edges = (uint32_t)da_count;
  out->cl_level = gpio_get_level(GPIO_RFID_LF_CARRIER_PIN);
  out->da_level = gpio_get_level(GPIO_RFID_LF_DATA_PIN);
  portENTER_CRITICAL(&s_capture_lock);
  out->capture_edges = s_capture_edges;
  out->dropped = s_dropped;
  portEXIT_CRITICAL(&s_capture_lock);
  return ESP_OK;
}

size_t rfid_lab_io_pop(rfid_lab_edge_t *out, size_t capacity) {
  if (out == NULL) return 0;
  size_t count = 0;
  portENTER_CRITICAL(&s_capture_lock);
  while (count < capacity && s_read_index != s_write_index) {
    out[count++] = s_ring[s_read_index % CAPTURE_RING_COUNT];
    s_read_index++;
  }
  portEXIT_CRITICAL(&s_capture_lock);
  return count;
}
