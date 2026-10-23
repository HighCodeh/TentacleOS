// SPDX-License-Identifier: GPL-3.0-or-later
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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"
#include "esp_log.h"

#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
#define WIFI_MODE_NULL 0
#define WIFI_MODE_APSTA 3
#define WIFI_BAND_MODE_2G_ONLY 1
#define WIFI_BAND_MODE_5G_ONLY 2
#define WIFI_BAND_MODE_AUTO 0
#define WIFI_PROMIS_FILTER_MASK_MGMT 1
#define WIFI_PROMIS_FILTER_MASK_DATA 2
#define WIFI_PROMIS_FILTER_MASK_CTRL 4
#define WIFI_SECOND_CHAN_NONE 0
#define WIFI_COUNTRY_POLICY_MANUAL 1
#define ESP_LOGW(tag, ...) capture_log(tag, __VA_ARGS__)
#define ESP_ERR_WIFI_NOT_STARTED 0x3002

typedef int wifi_mode_t;
typedef int wifi_band_mode_t;
typedef void (*wifi_promiscuous_cb_t)(void *, int);
typedef struct { uint32_t filter_mask; } wifi_promiscuous_filter_t;
typedef struct {
  uint8_t schan, nchan;
  int policy;
  uint32_t wifi_5g_channel_mask;
} wifi_country_t;
typedef struct {
  uint32_t duration_ms;
  uint8_t band;
  bool (*tick_cb)(void *);
  void *ctx;
} wifi_service_capture_config_t;

static const char *TAG = "test";
static void *s_mutex = (void *)1;
static bool s_is_active, s_is_connected, s_promiscuous_active;
static void *s_hopper_task_handle;
static int calls, fail_call;
static bool fail_stop, radio_started, promiscuous;
static wifi_mode_t mode;
static wifi_band_mode_t band;
static wifi_promiscuous_cb_t rx_cb;
static uint32_t filter_mask;
static int64_t now_us;
static unsigned ticks, cancel_tick, channels_visited;
static bool enable_5g;

static void capture_log(const char *tag, const char *format, ...) {
  (void)tag; (void)format;
}

static esp_err_t driver_result(void) { return ++calls == fail_call ? ESP_FAIL : ESP_OK; }
static int xSemaphoreTake(void *mutex, uint32_t timeout) {
  (void)mutex; (void)timeout; return pdTRUE;
}
static void xSemaphoreGive(void *mutex) { (void)mutex; }
static int64_t esp_timer_get_time(void) { return now_us; }
static void vTaskDelay(uint32_t ms) { now_us += (int64_t)ms * 1000; }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "test"; }
static esp_err_t esp_wifi_get_mode(wifi_mode_t *out) { *out = mode; return driver_result(); }
static esp_err_t esp_wifi_get_band_mode(wifi_band_mode_t *out) {
  *out = band; return driver_result();
}
static esp_err_t esp_wifi_get_promiscuous(bool *out) { *out = promiscuous; return driver_result(); }
static esp_err_t esp_wifi_get_promiscuous_filter(wifi_promiscuous_filter_t *out) {
  out->filter_mask = filter_mask; return driver_result();
}
static esp_err_t esp_wifi_get_country(wifi_country_t *out) {
  *out = (wifi_country_t){.schan = 1, .nchan = 11, .policy = WIFI_COUNTRY_POLICY_MANUAL,
                        .wifi_5g_channel_mask = 1u << 1};
  return driver_result();
}
static esp_err_t esp_wifi_set_mode(wifi_mode_t value) {
  esp_err_t err = driver_result();
  if (err == ESP_OK) {
    assert(value == WIFI_MODE_NULL || !radio_started);
    mode = value;
  }
  return err;
}
static esp_err_t esp_wifi_set_band_mode(wifi_band_mode_t value) {
  if (!radio_started) return ESP_ERR_WIFI_NOT_STARTED;
  esp_err_t err = driver_result(); if (err == ESP_OK) band = value; return err;
}
static esp_err_t esp_wifi_start(void) {
  assert(mode == WIFI_MODE_NULL);
  esp_err_t err = driver_result(); if (err == ESP_OK) radio_started = true; return err;
}
static esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *value) {
  esp_err_t err = driver_result(); if (err == ESP_OK) filter_mask = value->filter_mask; return err;
}
static esp_err_t esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_cb_t value) {
  esp_err_t err = driver_result(); if (err == ESP_OK) rx_cb = value; return err;
}
static esp_err_t esp_wifi_set_channel(uint8_t channel, int secondary) {
  (void)secondary;
  assert(channel <= 11 || channel == 36);
  if (channel == 36 && !enable_5g) return ESP_ERR_NOT_SUPPORTED;
  channels_visited++;
  return driver_result();
}
static esp_err_t esp_wifi_set_promiscuous(bool value) {
  if (value) assert(mode == WIFI_MODE_NULL && radio_started && rx_cb != NULL);
  esp_err_t err = driver_result(); if (err == ESP_OK) promiscuous = value; return err;
}
static esp_err_t esp_wifi_stop(void) {
  esp_err_t err = driver_result();
  if (fail_stop) return ESP_FAIL;
  if (err == ESP_OK) radio_started = false;
  return err;
}

#include "wifi_capture_under_test.inc"

static void receive(void *buf, int type) { (void)buf; (void)type; }
static bool tick(void *ctx) { (void)ctx; return ++ticks != cancel_tick; }
static void reset(void) {
  calls = fail_call = 0;
  fail_stop = radio_started = promiscuous = s_is_active = s_is_connected = false;
  s_promiscuous_active = false; s_hopper_task_handle = NULL;
  mode = WIFI_MODE_APSTA; band = WIFI_BAND_MODE_AUTO; filter_mask = 0x55; rx_cb = NULL;
  ticks = cancel_tick = channels_visited = 0; now_us = 0; enable_5g = false;
}

int main(void) {
  wifi_service_capture_config_t config = {.duration_ms = 1000, .band = 0, .tick_cb = tick};
  reset();
  assert(wifi_service_capture_passive(NULL, receive) == ESP_ERR_INVALID_ARG);
  assert(wifi_service_capture_passive(&config, NULL) == ESP_ERR_INVALID_ARG);
  s_is_active = true;
  assert(wifi_service_capture_passive(&config, receive) == ESP_ERR_INVALID_STATE && calls == 0);
  reset();
  assert(wifi_service_capture_passive(&config, receive) == ESP_OK);
  assert(ticks == 4 && channels_visited >= 4);
  assert(!radio_started && !promiscuous && rx_cb == NULL);
  assert(mode == WIFI_MODE_APSTA && band == WIFI_BAND_MODE_AUTO && filter_mask == 0x55);
  reset(); cancel_tick = 1;
  assert(wifi_service_capture_passive(&config, receive) == ESP_OK && ticks == 1);
  assert(!radio_started && !promiscuous);
  // Every driver setup/cleanup call may fail; none may start the radio in AP/STA mode.
  for (int failure = 1; failure <= 22; failure++) {
    reset(); fail_call = failure;
    esp_err_t err = wifi_service_capture_passive(&config, receive);
    if (calls >= failure) assert(err != ESP_OK || channels_visited > 1);
    if (radio_started) assert(mode == WIFI_MODE_NULL && s_is_active);
  }
  reset(); fail_stop = true;
  assert(wifi_service_capture_passive(&config, receive) != ESP_OK);
  assert(mode == WIFI_MODE_NULL && radio_started && s_is_active);
  assert(wifi_service_capture_passive(&config, receive) == ESP_ERR_INVALID_STATE);
  reset(); config.band = 2;
  assert(wifi_service_capture_passive(&config, receive) == ESP_ERR_NOT_SUPPORTED);
  assert(mode == WIFI_MODE_APSTA && !radio_started);
  reset(); enable_5g = true;
  assert(wifi_service_capture_passive(&config, receive) == ESP_OK);
  assert(!radio_started && !promiscuous);
  return 0;
}
