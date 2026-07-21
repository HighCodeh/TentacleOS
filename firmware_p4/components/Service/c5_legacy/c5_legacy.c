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

#include "c5_legacy.h"

#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "spi_bridge.h"
#include "spi_bridge_phy.h"

static const char *TAG = "C5_LEGACY";

// InkTest bridge protocol, mirrored from firmware_p4 bridge_proto.h in InkTest.
#define LEGACY_FRAME_SIZE   32
#define LEGACY_PAYLOAD_MAX  28
#define LEGACY_MAGIC_PONG   0xA5
#define LEGACY_STATUS_OK    0x00

#define LEGACY_CMD_NOP            0x00
#define LEGACY_CMD_PING           0x01
#define LEGACY_CMD_GET_INFO       0x02
#define LEGACY_CMD_ENTER_DOWNLOAD 0xF0

// Response is delivered on the next transfer, so each request costs two
// transfers with a settle window between them. Matches InkTest's bridge_request.
#define LEGACY_SETTLE_MS   50
#define LEGACY_MAX_ATTEMPTS 4
#define LEGACY_BACKOFF_CAP_MS 250

typedef struct __attribute__((packed)) {
  uint8_t cmd;
  uint8_t status;
  uint8_t len;
  uint8_t seq;
  uint8_t payload[LEGACY_PAYLOAD_MAX];
} legacy_frame_t;

_Static_assert(sizeof(legacy_frame_t) == LEGACY_FRAME_SIZE, "legacy frame must be 32 bytes");

// GET_INFO payload layout (slave -> master), mirrored from InkTest bridge_info_t.
typedef struct __attribute__((packed)) {
  uint8_t chip_model;
  uint8_t chip_revision;
  uint8_t wifi_mac[6];
  uint8_t ble_mac[6];
  uint32_t free_heap_kb;
  uint8_t reserved[10];
} legacy_info_t;

// DMA-capable, word-aligned scratch buffers. The legacy client is single
// consumer (console task, one command at a time), so static buffers are safe.
static WORD_ALIGNED_ATTR uint8_t s_tx[LEGACY_FRAME_SIZE];
static WORD_ALIGNED_ATTR uint8_t s_rx[LEGACY_FRAME_SIZE];
static uint8_t s_seq = 0;

static esp_err_t legacy_transfer(const legacy_frame_t *out, legacy_frame_t *in) {
  memcpy(s_tx, out, LEGACY_FRAME_SIZE);
  memset(s_rx, 0, LEGACY_FRAME_SIZE);
  esp_err_t ret = spi_bridge_phy_legacy_transmit(s_tx, s_rx, LEGACY_FRAME_SIZE);
  if (ret == ESP_OK && in != NULL) {
    memcpy(in, s_rx, LEGACY_FRAME_SIZE);
  }
  return ret;
}

// One request/response round using the InkTest two-transfer scheme: transfer 1
// delivers the request (slave returns whatever it had queued), then after a
// settle the NOP transfer reads the actual response. Retries with growing
// backoff, exactly like InkTest's master, to absorb the slave re-arming its
// response queue between rounds.
static esp_err_t legacy_request(uint8_t cmd, legacy_frame_t *resp) {
  uint32_t backoff = LEGACY_SETTLE_MS;

  for (int attempt = 0; attempt < LEGACY_MAX_ATTEMPTS; attempt++) {
    legacy_frame_t req = {.cmd = cmd, .seq = ++s_seq};

    esp_err_t ret = legacy_transfer(&req, NULL); // transfer 1: deliver request
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "cmd 0x%02X transfer1 failed: %s", cmd, esp_err_to_name(ret));
      return ret;
    }

    if (backoff > 0) {
      vTaskDelay(pdMS_TO_TICKS(backoff));
    }

    legacy_frame_t nop = {.cmd = LEGACY_CMD_NOP, .seq = s_seq};
    ret = legacy_transfer(&nop, resp); // transfer 2: read the response
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "cmd 0x%02X poll failed: %s", cmd, esp_err_to_name(ret));
      return ret;
    }

    if (resp->cmd == cmd) {
      if (attempt > 0) {
        ESP_LOGI(TAG, "cmd 0x%02X recovered on attempt %d", cmd, attempt);
      }
      return ESP_OK;
    }

    ESP_LOGW(TAG, "cmd mismatch (attempt %d): sent 0x%02X, got 0x%02X status 0x%02X", attempt,
             cmd, resp->cmd, resp->status);

    // Drain a possibly stale response before retrying.
    legacy_frame_t drain = {.cmd = LEGACY_CMD_NOP, .seq = s_seq};
    legacy_transfer(&drain, NULL);

    if (backoff < LEGACY_BACKOFF_CAP_MS) {
      backoff += 50;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  return ESP_ERR_INVALID_RESPONSE;
}

static esp_err_t legacy_session_begin(void) {
  spi_bridge_suspend(true);
  esp_err_t ret = spi_bridge_phy_legacy_begin();
  if (ret != ESP_OK) {
    spi_bridge_suspend(false);
  }
  return ret;
}

static void legacy_session_end(void) {
  spi_bridge_phy_legacy_end();
  spi_bridge_suspend(false);
}

esp_err_t c5_legacy_ping(void) {
  esp_err_t ret = legacy_session_begin();
  if (ret != ESP_OK) {
    return ret;
  }
  legacy_frame_t resp = {0};
  ret = legacy_request(LEGACY_CMD_PING, &resp);
  legacy_session_end();

  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "no PING reply - C5 is not responding on the InkTest protocol");
    return ret;
  }
  if (resp.status == LEGACY_STATUS_OK && resp.len >= 1 && resp.payload[0] == LEGACY_MAGIC_PONG) {
    ESP_LOGI(TAG, "PONG received - C5 is alive and running InkTest");
    return ESP_OK;
  }
  ESP_LOGW(TAG, "unexpected PING reply: status=0x%02X len=%u payload[0]=0x%02X", resp.status,
           resp.len, resp.payload[0]);
  return ESP_ERR_INVALID_RESPONSE;
}

esp_err_t c5_legacy_get_info(void) {
  esp_err_t ret = legacy_session_begin();
  if (ret != ESP_OK) {
    return ret;
  }
  legacy_frame_t resp = {0};
  ret = legacy_request(LEGACY_CMD_GET_INFO, &resp);
  legacy_session_end();

  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "no GET_INFO reply - C5 not responding on the InkTest protocol");
    return ret;
  }
  if (resp.status != LEGACY_STATUS_OK || resp.len < sizeof(legacy_info_t)) {
    ESP_LOGW(TAG, "unexpected GET_INFO reply: status=0x%02X len=%u", resp.status, resp.len);
    return ESP_ERR_INVALID_RESPONSE;
  }

  legacy_info_t info;
  memcpy(&info, resp.payload, sizeof(info));
  ESP_LOGI(TAG, "C5 info (InkTest): chip_model=%u rev=%u free_heap=%luKB", info.chip_model,
           info.chip_revision, (unsigned long)info.free_heap_kb);
  ESP_LOGI(TAG, "  WiFi MAC %02x:%02x:%02x:%02x:%02x:%02x", info.wifi_mac[0], info.wifi_mac[1],
           info.wifi_mac[2], info.wifi_mac[3], info.wifi_mac[4], info.wifi_mac[5]);
  ESP_LOGI(TAG, "  BLE  MAC %02x:%02x:%02x:%02x:%02x:%02x", info.ble_mac[0], info.ble_mac[1],
           info.ble_mac[2], info.ble_mac[3], info.ble_mac[4], info.ble_mac[5]);
  return ESP_OK;
}

esp_err_t c5_legacy_enter_download(void) {
  esp_err_t ret = legacy_session_begin();
  if (ret != ESP_OK) {
    return ret;
  }
  ESP_LOGW(TAG, "commanding InkTest C5 into ROM download mode...");
  legacy_frame_t resp = {0};
  ret = legacy_request(LEGACY_CMD_ENTER_DOWNLOAD, &resp);
  legacy_session_end();

  // The C5 sends the OK on one more transfer, then reboots into the ROM stub.
  // Either a clean ack or a lost reply (it already rebooted) counts as success.
  if (ret == ESP_OK && resp.status == LEGACY_STATUS_OK) {
    ESP_LOGW(TAG, "C5 acknowledged enter-download - it is rebooting into ROM download mode");
    ESP_LOGW(TAG, "now run 'c5 rom' to flash the embedded TentacleOS image");
    return ESP_OK;
  }
  ESP_LOGW(TAG, "no clean ack (ret=%s) - the C5 may have rebooted anyway; try 'c5 rom'",
           esp_err_to_name(ret));
  // Treat as success: enter-download reboots the C5, so a missing reply is
  // expected and the operator should proceed to the ROM flash regardless.
  return ESP_OK;
}
