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

// C5 log relay on the P4. The C5 tees its ESP_LOGx output and streams each line
// to the P4 over SPI_ID_SYSTEM_LOG as [level u8][utf-8 text]. This module
// consumes that stream and re-emits each line as a host-link LOG frame tagged
// source=C5, so the companion app can show a separate C5 console.

#include "host_link.h"

#include "esp_log.h"

#include "spi_bridge.h"
#include "spi_protocol.h"

static const char *TAG = "HOST_LINK_C5LOG";

static void on_c5_log_stream(spi_id_t id, const uint8_t *payload, uint8_t len) {
  (void)id;
  // Record = [level u8][utf-8 text]; at least the level byte must be present.
  if (payload == NULL || len < 1) {
    return;
  }
  uint8_t level = payload[0];
  const char *text = (const char *)(payload + 1);
  size_t text_len = (size_t)(len - 1);
  host_link_emit_log(HOST_LOG_SRC_C5, (host_log_level_t)level, text, text_len);
}

esp_err_t host_link_c5log_init(void) {
  spi_bridge_register_stream_cb(SPI_ID_SYSTEM_LOG, on_c5_log_stream);
  ESP_LOGI(TAG, "C5 log relay registered");
  return ESP_OK;
}
