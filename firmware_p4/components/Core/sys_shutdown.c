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

#include "sys_shutdown.h"

#include "esp_log.h"
#include "esp_system.h"

#include "storage_init.h"

static const char *TAG = "SYS_SHUTDOWN";

static void flush_state(void);

esp_err_t sys_shutdown_init(void) {
  esp_err_t ret = esp_register_shutdown_handler(flush_state);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to register shutdown handler: %s", esp_err_to_name(ret));
    return ret;
  }

  ESP_LOGI(TAG, "Shutdown handler registered");
  return ret;
}

static void flush_state(void) {
  (void)storage_deinit();
}
