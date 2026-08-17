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

#include "deauther_detector.h"

#include "led_control.h"

#include <string.h>

#include "esp_log.h"

#include "spi_bridge.h"
#include "spi_session.h"

static const char *TAG = "DEAUTHER_DETECTOR";

static uint32_t s_session_id = SPI_SESSION_INVALID_ID;

void deauther_detector_start(void) {
  s_session_id = spi_session_start(SPI_ID_WIFI_APP_DEAUTH_DET, NULL, 0, NULL, NULL);
  if (s_session_id == SPI_SESSION_INVALID_ID) {
    ESP_LOGW(TAG, "Failed to start deauth detector over SPI");
    led_signal_error();
  } else {
    led_signal_info();
  }
}

void deauther_detector_stop(void) {
  if (s_session_id != SPI_SESSION_INVALID_ID) {
    spi_session_stop(s_session_id);
    s_session_id = SPI_SESSION_INVALID_ID;
  }
}

uint32_t deauther_detector_get_count(void) {
  spi_header_t resp;
  uint8_t payload[4];
  uint16_t index = SPI_DATA_INDEX_DEAUTH_COUNT;
  if (spi_bridge_send_command(
          SPI_ID_SYSTEM_DATA, (uint8_t *)&index, 2, &resp, payload, sizeof(payload), 1000) ==
      ESP_OK) {
    uint32_t count = 0;
    memcpy(&count, payload, sizeof(count));
    return count;
  }
  return 0;
}
