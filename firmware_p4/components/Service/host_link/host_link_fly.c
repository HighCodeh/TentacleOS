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

#include "host_link_fly.h"

#include "esp_log.h"

#include "fly_link.h"
#include "spi_protocol.h"

static const char *TAG = "HOST_FLY";

uint8_t host_fly_handle(uint16_t cmd,
                        const uint8_t *payload,
                        uint16_t plen,
                        uint8_t *out,
                        uint16_t out_cap,
                        uint16_t *out_len) {
  uint8_t result = fly_link_handle(cmd, payload, plen, out, out_cap, out_len);
  if (result != SPI_STATUS_OK)
    ESP_LOGD(TAG, "Command 0x%04x status %u", cmd, result);
  return result;
}
