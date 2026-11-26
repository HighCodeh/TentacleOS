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

#ifndef ETHERNET_SHARE_H
#define ETHERNET_SHARE_H
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
  ETHERNET_SHARE_IDLE,
  ETHERNET_SHARE_UNPLUG,
  ETHERNET_SHARE_WAIT_ADAPTER,
  ETHERNET_SHARE_WAIT_LINK,
  ETHERNET_SHARE_DHCP,
  ETHERNET_SHARE_ONLINE,
  ETHERNET_SHARE_STOPPING,
  ETHERNET_SHARE_ERROR,
  ETHERNET_SHARE_STATE_COUNT
} ethernet_share_state_t;
typedef enum {
  ETHERNET_SHARE_STAGE_POWER_LEASE,
  ETHERNET_SHARE_STAGE_USB_CLAIM,
  ETHERNET_SHARE_STAGE_TYPEC,
  ETHERNET_SHARE_STAGE_USB_DRIVER,
  ETHERNET_SHARE_STAGE_USB_ROUTE,
  ETHERNET_SHARE_STAGE_ATTACHMENT,
  ETHERNET_SHARE_STAGE_USB_POWER,
  ETHERNET_SHARE_STAGE_POWER_CHECK,
  ETHERNET_SHARE_STAGE_ADAPTER,
  ETHERNET_SHARE_STAGE_ROUTER_START,
  ETHERNET_SHARE_STAGE_ROUTER_STATUS,
  ETHERNET_SHARE_STAGE_FORWARDING,
  ETHERNET_SHARE_STAGE_C5_RX,
  ETHERNET_SHARE_STAGE_C5_TX,
  ETHERNET_SHARE_STAGE_USB_TX,
  ETHERNET_SHARE_STAGE_PRESET,
  ETHERNET_SHARE_STAGE_COUNT
} ethernet_share_stage_t;
#define ETHERNET_SHARE_POWER_DETAIL_SIZE 128
typedef struct {
  ethernet_share_state_t state;
  ethernet_share_stage_t stage;
  esp_err_t error;
  uint32_t ip;
  uint32_t received, transmitted;
  char ssid[33];
  char power_detail[ETHERNET_SHARE_POWER_DETAIL_SIZE];
} ethernet_share_snapshot_t;
esp_err_t ethernet_share_start(void);
void ethernet_share_stop(void);
void ethernet_share_get(ethernet_share_snapshot_t *out);
const char *ethernet_share_stage_name(ethernet_share_stage_t stage);
#ifdef __cplusplus
}
#endif
#endif
