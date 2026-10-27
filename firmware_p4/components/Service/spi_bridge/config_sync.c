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

#include "config_sync.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_crc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "spi_bridge.h"
#include "spi_protocol.h"
#include "sys_prio.h"

static const char *TAG = "CONFIG_SYNC";

#define CFG_CHUNK_BYTES 240 // payload = [id u8][offset u32] + data, capped by SPI_MAX_PAYLOAD
#define CFG_MAX_BYTES   4096
#define CFG_CMD_TIMEOUT 3000

// Master config paths on the P4 (assets partition), indexed by spi_cfg_id_t.
static const char *const s_cfg_paths[SPI_CFG_COUNT] = {
    [SPI_CFG_WIFI_AP] = "/assets/config/wifi/wifi_ap.conf",
    [SPI_CFG_BLE_ANNOUNCE] = "/assets/config/bluetooth/ble_announce.conf",
    [SPI_CFG_BLE_SPAM] = "/assets/config/bluetooth/beacon_list.conf",
    [SPI_CFG_CHAT] = "/assets/config/chat/chat.conf",
    [SPI_CFG_CHAT_ADDRS] = "/assets/config/chat/addresses.conf",
    [SPI_CFG_GROK] = "/sdcard/config/grok.json",
};

// Read a master config into a freshly malloc'd buffer. Returns bytes read (0 on
// error / missing file); caller frees *out.
static void clear_free(uint8_t *buf, size_t size) {
  if (!buf)
    return;
  volatile uint8_t *p = buf;
  while (size--)
    *p++ = 0;
  free(buf);
}

static size_t read_master(const char *path, uint8_t **out) {
  *out = NULL;
  FILE *f = fopen(path, "rb");
  if (f == NULL)
    return 0;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0 || size > CFG_MAX_BYTES) {
    fclose(f);
    return 0;
  }
  uint8_t *buf = (uint8_t *)malloc(size);
  if (buf == NULL) {
    fclose(f);
    return 0;
  }
  size_t rd = fread(buf, 1, size, f);
  fclose(f);
  if (rd != (size_t)size) {
    clear_free(buf, (size_t)size);
    return 0;
  }
  *out = buf;
  return rd;
}

static SemaphoreHandle_t s_sync_mutex;

static esp_err_t push_locked(uint8_t id, bool (*keep_running)(void)) {
  if (id >= SPI_CFG_COUNT || s_cfg_paths[id] == NULL)
    return ESP_ERR_INVALID_ARG;

  uint8_t *buf = NULL;
  size_t size = read_master(s_cfg_paths[id], &buf);
  if (size == 0)
    return ESP_ERR_NOT_FOUND; // no master to push (fine: nothing to sync)

  uint32_t crc = esp_crc32_le(0, buf, size);
  const uint32_t timeout_ms = id == SPI_CFG_GROK ? 300 : CFG_CMD_TIMEOUT;
#define CFG_CONTINUE()                     \
  do {                                     \
    if (keep_running && !keep_running()) { \
      clear_free(buf, size);               \
      return ESP_ERR_INVALID_STATE;        \
    }                                      \
  } while (0)
  CFG_CONTINUE();

  spi_header_t resp;
  uint8_t rbuf[8] = {0};

  // Skip the push if the C5 already holds this exact content.
  uint8_t idb = id;
  if (spi_bridge_send_command(
          SPI_ID_SYSTEM_CFG_HASH, &idb, 1, &resp, rbuf, sizeof(rbuf), timeout_ms) == ESP_OK &&
      resp.length == 4) {
    uint32_t c5hash = 0;
    memcpy(&c5hash, rbuf, sizeof(c5hash));
    if (c5hash == crc) {
      clear_free(buf, size);
      return ESP_OK; // up to date
    }
  }

  spi_cfg_begin_t begin = {.id = id, .size = (uint32_t)size, .hash = crc};
  CFG_CONTINUE();
  if (spi_bridge_send_command(SPI_ID_SYSTEM_CFG_BEGIN,
                              (const uint8_t *)&begin,
                              sizeof(begin),
                              &resp,
                              rbuf,
                              sizeof(rbuf),
                              timeout_ms) != ESP_OK) {
    clear_free(buf, size);
    return ESP_FAIL;
  }

  uint8_t chunk[5 + CFG_CHUNK_BYTES];
  for (size_t off = 0; off < size; off += CFG_CHUNK_BYTES) {
    CFG_CONTINUE();
    uint16_t n = (size - off > CFG_CHUNK_BYTES) ? CFG_CHUNK_BYTES : (uint16_t)(size - off);
    uint32_t off32 = (uint32_t)off;
    chunk[0] = id;
    memcpy(chunk + 1, &off32, sizeof(off32));
    memcpy(chunk + 5, buf + off, n);
    if (spi_bridge_send_command(
            SPI_ID_SYSTEM_CFG_CHUNK, chunk, 5 + n, &resp, rbuf, sizeof(rbuf), timeout_ms) !=
        ESP_OK) {
      clear_free(buf, size);
      return ESP_FAIL;
    }
  }
  clear_free(buf, size);
  buf = NULL;
  CFG_CONTINUE();

  esp_err_t ret = spi_bridge_send_command(
      SPI_ID_SYSTEM_CFG_COMMIT, &idb, 1, &resp, rbuf, sizeof(rbuf), timeout_ms);
  if (ret == ESP_OK)
    ESP_LOGI(TAG, "cfg %u synced to C5 (%u bytes, hash 0x%08x)", id, (unsigned)size, (unsigned)crc);
  return ret;
#undef CFG_CONTINUE
}

esp_err_t config_sync_push_cancellable(uint8_t id, bool (*keep_running)(void)) {
  SemaphoreHandle_t mutex = __atomic_load_n(&s_sync_mutex, __ATOMIC_ACQUIRE);
  if (!mutex) {
    SemaphoreHandle_t candidate = xSemaphoreCreateMutex(), expected = NULL;
    if (!candidate)
      return ESP_ERR_NO_MEM;
    if (!__atomic_compare_exchange_n(
            &s_sync_mutex, &expected, candidate, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
      vSemaphoreDelete(candidate);
    mutex = __atomic_load_n(&s_sync_mutex, __ATOMIC_ACQUIRE);
  }
  if (xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  esp_err_t ret = push_locked(id, keep_running);
  xSemaphoreGive(mutex);
  return ret;
}

esp_err_t config_sync_push(uint8_t id) {
  return config_sync_push_cancellable(id, NULL);
}

static void config_sync_task(void *arg) {
  (void)arg;
  spi_header_t resp;
  uint8_t rbuf[8];

  // Wait for the C5 to answer (bridge up + C5 booted) before pushing, so a
  // late-booting C5 is still synced. Bounded so we never spin forever.
  for (int i = 0; i < 50; i++) {
    if (spi_bridge_send_command(SPI_ID_SYSTEM_PING, NULL, 0, &resp, rbuf, sizeof(rbuf), 1000) ==
        ESP_OK)
      break;
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  vTaskDelay(pdMS_TO_TICKS(500)); // let the C5 mount its assets partition

  int synced = 0;
  for (uint8_t id = 0; id < SPI_CFG_COUNT; id++) {
    // Local pairing credentials are provisioned only on explicit voice start.
    if (id == SPI_CFG_GROK)
      continue;
    if (config_sync_push(id) == ESP_OK)
      synced++;
  }
  ESP_LOGI(TAG, "config sync done (%d/%d ok)", synced, SPI_CFG_COUNT);
  vTaskDelete(NULL);
}

void config_sync_start(void) {
  xTaskCreatePinnedToCore(
      config_sync_task, "cfg_sync", 4096, NULL, SYS_PRIO_BACKGROUND, NULL, SYS_CORE_RADIO);
}
