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

#include "ota_service.h"

#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "OTA_SVC";

// UART0 is wired to the P4. On the ESP32-C5 the UART0 silicon pins are
// U0RXD = GPIO12 and U0TXD = GPIO11 - the C5 receives on 12 and transmits on
// 11. The console lives on USB-Serial/JTAG now, so UART0 is dedicated to the
// firmware transfer.
#define OTA_UART        UART_NUM_0
#define OTA_UART_RX_PIN 12
#define OTA_UART_TX_PIN 11
// Conservative bring-up rate: 115200 carries cleanly over plain jumper wiring.
// Corruption at higher rates makes esp_ota_end reject the image. Raise later.
#define OTA_BAUD        115200
#define OTA_RX_RINGBUF  (16 * 1024)
// Per-block flow control: we ACK each block after writing it to flash and the
// P4 only sends the next one then, so the RX ring buffer can never overflow
// during a flash-write/scheduling stall.
#define OTA_BLOCK       4096

// Handshake: the P4 sends OTA_MAGIC and waits for us to reply OTA_READY before
// it sends the 4-byte little-endian size + the raw app binary. The reply makes
// the size/data alignment deterministic even if the first magic byte is lost on
// the idle->active line transition (the P4 just retries the magic).
static const uint8_t OTA_MAGIC[4] = {0xC5, 0xFA, 0x5E, 0x01};
#define OTA_READY 0x52
#define OTA_ACK   0x06
#define OTA_NAK   0x15

// Plausible C5 app image size bounds - guards against acting on a spurious
// magic match. Min ~64 KB, max = the 2 MB OTA partition.
#define OTA_MIN_SIZE 0x10000
#define OTA_MAX_SIZE 0x200000

static void send_status(uint8_t s) {
  uart_write_bytes(OTA_UART, (const char *)&s, 1);
  uart_wait_tx_done(OTA_UART, pdMS_TO_TICKS(200));
}

static void wait_for_magic(void) {
  size_t matched = 0;
  uint8_t b;
  while (matched < sizeof(OTA_MAGIC)) {
    if (uart_read_bytes(OTA_UART, &b, 1, portMAX_DELAY) != 1) {
      continue;
    }
    if (b == OTA_MAGIC[matched]) {
      matched++;
    } else {
      matched = (b == OTA_MAGIC[0]) ? 1 : 0;
    }
  }
}

static esp_err_t read_exact(uint8_t *buf, uint32_t len, uint32_t timeout_ms) {
  uint32_t got = 0;
  while (got < len) {
    int n = uart_read_bytes(OTA_UART, buf + got, len - got, pdMS_TO_TICKS(timeout_ms));
    if (n <= 0) {
      return ESP_ERR_TIMEOUT;
    }
    got += (uint32_t)n;
  }
  return ESP_OK;
}

static void do_ota(void) {
  uint8_t size_buf[4];
  if (read_exact(size_buf, sizeof(size_buf), 2000) != ESP_OK) {
    ESP_LOGE(TAG, "size header timeout");
    return;
  }
  uint32_t size = (uint32_t)size_buf[0] | ((uint32_t)size_buf[1] << 8) |
                  ((uint32_t)size_buf[2] << 16) | ((uint32_t)size_buf[3] << 24);
  ESP_LOGW(TAG, "OTA push: %lu bytes", (unsigned long)size);

  if (size < OTA_MIN_SIZE || size > OTA_MAX_SIZE) {
    ESP_LOGE(TAG, "implausible size %lu - ignoring (out of sync?)", (unsigned long)size);
    send_status(OTA_NAK);
    return;
  }

  const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
  if (part == NULL) {
    ESP_LOGE(TAG, "no OTA partition available");
    send_status(OTA_NAK);
    return;
  }
  ESP_LOGI(TAG, "target partition '%s' @ 0x%lx", part->label, (unsigned long)part->address);

  esp_ota_handle_t handle = 0;
  esp_err_t err = esp_ota_begin(part, size, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
    send_status(OTA_NAK);
    return;
  }
  // Partition is now erased. Only now tell the P4 to start sending blocks - if
  // it sent during the (multi-second) erase above, those bytes would be lost
  // while the flash cache is disabled.
  ESP_LOGI(TAG, "partition erased - ready for data");
  send_status(OTA_ACK);

  static uint8_t buf[OTA_BLOCK];
  uint32_t remaining = size;
  while (remaining > 0) {
    uint32_t chunk = remaining > OTA_BLOCK ? OTA_BLOCK : remaining;
    if (read_exact(buf, chunk, 5000) != ESP_OK) {
      ESP_LOGE(TAG, "data timeout @ %lu/%lu", (unsigned long)(size - remaining),
               (unsigned long)size);
      esp_ota_abort(handle);
      send_status(OTA_NAK);
      return;
    }
    err = esp_ota_write(handle, buf, chunk);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(err));
      esp_ota_abort(handle);
      send_status(OTA_NAK);
      return;
    }
    remaining -= chunk;
    // Block written - tell the P4 to send the next one (flow control).
    send_status(OTA_ACK);
    uint32_t written = size - remaining;
    if ((written % (256 * 1024)) < OTA_BLOCK) {
      ESP_LOGI(TAG, "  received %lu/%lu", (unsigned long)written, (unsigned long)size);
    }

    vTaskDelay(1); // yield each block so the idle task runs and feeds the task WDT
  }
  ESP_LOGI(TAG, "all %lu bytes received, validating image...", (unsigned long)size);

  err = esp_ota_end(handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_end (image invalid?): %s", esp_err_to_name(err));
    send_status(OTA_NAK);
    return;
  }
  err = esp_ota_set_boot_partition(part);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(err));
    send_status(OTA_NAK);
    return;
  }

  ESP_LOGW(TAG, "OTA OK - booting '%s'", part->label);
  send_status(OTA_ACK);
  vTaskDelay(pdMS_TO_TICKS(200));
  esp_restart();
}

static void ota_task(void *arg) {
  (void)arg;
  const esp_partition_t *running = esp_ota_get_running_partition();
  ESP_LOGI(TAG, "OTA receiver ready (running from '%s')", running ? running->label : "?");
  while (true) {
    wait_for_magic();
    ESP_LOGW(TAG, "OTA sync received from P4");
    // Discard anything trailing the magic, then tell the P4 we're aligned. The
    // P4 only sends the size + image after seeing this, so the next bytes we
    // read are guaranteed to be the size header.
    uart_flush_input(OTA_UART);
    send_status(OTA_READY);
    do_ota();
  }
}

esp_err_t ota_service_start(void) {
  const uart_config_t cfg = {
      .baud_rate = OTA_BAUD,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  esp_err_t err = uart_driver_install(OTA_UART, OTA_RX_RINGBUF, 0, 0, NULL, 0);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "uart_driver_install: %s", esp_err_to_name(err));
    return err;
  }
  ESP_ERROR_CHECK(uart_param_config(OTA_UART, &cfg));
  ESP_ERROR_CHECK(uart_set_pin(OTA_UART, OTA_UART_TX_PIN, OTA_UART_RX_PIN, UART_PIN_NO_CHANGE,
                               UART_PIN_NO_CHANGE));

  if (xTaskCreate(ota_task, "ota_task", 6144, NULL, 5, NULL) != pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
