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

#include "power_manager.h"

#include <stdatomic.h>

#include "esp_log.h"

#include "board_service.h"
#include "bluetooth_service.h"
#include "ir_remote.h"
#include "grok_wifi.h"
#include "service_esp_now.h"
#include "ota_service.h"
#include "session_manager.h"
#include "spi_bridge.h"
#include "wifi_service.h"

static const char *TAG = "POWER_MGR";
static atomic_bool s_is_ready;

void power_manager_set_ready(void) {
  atomic_store(&s_is_ready, true);
  ESP_LOGI(TAG, "Idle sleep available after SPI drain; active operations retain wakefulness");
}

bool power_manager_can_sleep(void) {
  if (!atomic_load(&s_is_ready) || wifi_service_is_active() || wifi_service_is_busy() ||
      bluetooth_service_is_initialized() || session_manager_is_active() || grok_wifi_is_active() ||
      service_esp_now_is_online() || spi_bridge_async_scan_busy() || !ir_remote_can_sleep() ||
      !board_service_can_sleep())
    return false;
  spi_ota_status_t status;
  ota_service_get_status(&status);
  return status.state == SPI_OTA_STATE_IDLE;
}
