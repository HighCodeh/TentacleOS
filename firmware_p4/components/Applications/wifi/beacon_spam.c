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

#include "beacon_spam.h"

#include "led_control.h"

#include <string.h>

#include "esp_log.h"

#include "spi_bridge.h"
#include "spi_session.h"

static const char *TAG = "BEACON_SPAM";

static bool s_is_running = false;
static uint32_t s_session_id = SPI_SESSION_INVALID_ID;

bool beacon_spam_start_custom(const char *json_path) {
  if (json_path == NULL)
    return false;
  size_t len = strnlen(json_path, SPI_MAX_PAYLOAD - 1);
  s_session_id = spi_session_start(
      SPI_ID_WIFI_APP_BEACON_SPAM, (uint8_t *)json_path, (uint8_t)len, NULL, NULL);
  s_is_running = (s_session_id != SPI_SESSION_INVALID_ID);
  s_is_running ? led_signal_info() : led_signal_error();
  if (!s_is_running)
    ESP_LOGW(TAG, "Failed to start beacon spam (custom list) over SPI");
  return s_is_running;
}

bool beacon_spam_start_random(void) {
  s_session_id = spi_session_start(SPI_ID_WIFI_APP_BEACON_SPAM, NULL, 0, NULL, NULL);
  s_is_running = (s_session_id != SPI_SESSION_INVALID_ID);
  s_is_running ? led_signal_info() : led_signal_error();
  if (!s_is_running)
    ESP_LOGW(TAG, "Failed to start beacon spam (random) over SPI");
  return s_is_running;
}

void beacon_spam_stop(void) {
  if (s_session_id != SPI_SESSION_INVALID_ID) {
    spi_session_stop(s_session_id);
    s_session_id = SPI_SESSION_INVALID_ID;
  }
  s_is_running = false;
}

bool beacon_spam_is_running(void) {
  return s_is_running;
}
