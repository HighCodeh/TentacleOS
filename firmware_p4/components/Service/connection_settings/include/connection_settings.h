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

#ifndef CONNECTION_SETTINGS_H
#define CONNECTION_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "tos_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONNECTION_SETTINGS_WIFI_MAX_CLIENTS 8
#define CONNECTION_SETTINGS_BLE_MAX_CLIENTS  TOS_CONFIG_BLE_MAX_CLIENTS
#define CONNECTION_SETTINGS_NAME_MAX         29
#define CONNECTION_SETTINGS_PASSWORD_MIN     8
#define CONNECTION_SETTINGS_PASSWORD_MAX     63

typedef enum {
  CONNECTION_SETTINGS_WIFI,
  CONNECTION_SETTINGS_BLE,
  CONNECTION_SETTINGS_COUNT
} connection_settings_kind_t;

typedef struct {
  bool is_busy;
  bool has_result;
  bool is_applied;
  esp_err_t error;
  connection_settings_kind_t kind;
  uint32_t revision;
} connection_settings_status_t;

esp_err_t connection_settings_request_wifi(const tos_config_wifi_t *config);
esp_err_t connection_settings_request_ble(const tos_config_ble_t *config);
void connection_settings_get_status(connection_settings_status_t *out_status);
void connection_settings_get_config(tos_config_wifi_t *out_wifi, tos_config_ble_t *out_ble);
esp_err_t connection_settings_validate_wifi(const tos_config_wifi_t *config);
esp_err_t connection_settings_validate_ble(const tos_config_ble_t *config);

#ifdef __cplusplus
}
#endif
#endif
