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

#include "c5_flasher.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pin_def.h"
#include "spi_bridge.h"
#include "spi_protocol.h"

static const char *TAG = "C5_FLASHER";

static volatile uint32_t s_ota_sent = 0;
static volatile uint32_t s_ota_total = 0;

void c5_flasher_progress(uint32_t *sent, uint32_t *total) {
  if (sent != NULL)
    *sent = s_ota_sent;
  if (total != NULL)
    *total = s_ota_total;
}

#define OTA_UART UART_NUM_1

#define OTA_BAUD     115200
#define OTA_UART_BUF 4096

#define OTA_BLOCK            4096
#define OTA_BLOCK_TIMEOUT_MS 5000

#define OTA_BEGIN_TIMEOUT_MS 20000

#define C5_SD_FW_PATH "/sdcard/c5/TentacleOS_C5.bin"

#define OTA_READY 0x52
#define OTA_ACK   0x06
#define OTA_NAK   0x15

#define OTA_SYNC_ATTEMPTS    10
#define OTA_READY_TIMEOUT_MS 1000
#define OTA_ACK_TIMEOUT_MS   30000

#define ENTER_DOWNLOAD_TIMEOUT_MS 500
#define START_UART_OTA_TIMEOUT_MS 1000

esp_err_t c5_flasher_init(void) {
  const uart_config_t cfg = {
      .baud_rate = OTA_BAUD,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  if (!uart_is_driver_installed(OTA_UART)) {
    esp_err_t err = uart_driver_install(OTA_UART, OTA_UART_BUF, 0, 0, NULL, 0);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "uart_driver_install: %s", esp_err_to_name(err));
      return err;
    }
  }
  ESP_ERROR_CHECK(uart_param_config(OTA_UART, &cfg));
  esp_err_t pin_err = uart_set_pin(
      OTA_UART, GPIO_C5_UART_TX_PIN, GPIO_C5_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (pin_err != ESP_OK) {
    ESP_LOGE(TAG, "uart_set_pin: %s", esp_err_to_name(pin_err));
    return pin_err;
  }
  ESP_LOGI(TAG,
           "C5 OTA UART ready: UART%d @ %d baud, TX=GPIO%d -> C5 RX, RX=GPIO%d <- C5 TX",
           OTA_UART,
           OTA_BAUD,
           GPIO_C5_UART_TX_PIN,
           GPIO_C5_UART_RX_PIN);
  return ESP_OK;
}

esp_err_t c5_flasher_enter_download(void) {
  ESP_LOGW(TAG, "Requesting C5 to enter ROM download mode over SPI...");

  spi_header_t resp = {0};
  esp_err_t ret = spi_bridge_send_command(
      SPI_ID_SYSTEM_ENTER_DOWNLOAD, NULL, 0, &resp, NULL, ENTER_DOWNLOAD_TIMEOUT_MS);
  if (ret == ESP_OK || ret == ESP_ERR_TIMEOUT) {
    vTaskDelay(pdMS_TO_TICKS(300));
    return ESP_OK;
  }
  ESP_LOGE(TAG, "enter-download command failed: %s", esp_err_to_name(ret));
  return ret;
}

void c5_flasher_release_uart(void) {
  if (uart_is_driver_installed(OTA_UART))
    uart_driver_delete(OTA_UART);

  gpio_reset_pin(GPIO_C5_UART_TX_PIN);
  gpio_reset_pin(GPIO_C5_UART_RX_PIN);
  gpio_set_direction(GPIO_C5_UART_TX_PIN, GPIO_MODE_INPUT);
  gpio_set_direction(GPIO_C5_UART_RX_PIN, GPIO_MODE_INPUT);
  gpio_set_pull_mode(GPIO_C5_UART_TX_PIN, GPIO_FLOATING);
  gpio_set_pull_mode(GPIO_C5_UART_RX_PIN, GPIO_FLOATING);
  ESP_LOGW(TAG,
           "C5 UART lines released: GPIO%d/GPIO%d now hi-Z inputs.",
           GPIO_C5_UART_TX_PIN,
           GPIO_C5_UART_RX_PIN);
  ESP_LOGW(TAG, "External USB-serial can now own the C5 UART. Reboot P4 to restore.");
}

esp_err_t c5_flasher_update(const uint8_t *bin_data, uint32_t bin_size) {
  FILE *f = NULL;
  uint8_t *block = NULL;
  esp_err_t result = ESP_FAIL;

  if (bin_data == NULL) {
    f = fopen(C5_SD_FW_PATH, "rb");
    if (f == NULL) {
      ESP_LOGE(TAG, "C5 firmware not on SD: %s (is the card inserted?)", C5_SD_FW_PATH);
      return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz <= 0) {
      ESP_LOGE(TAG, "C5 firmware on SD is empty: %s", C5_SD_FW_PATH);
      fclose(f);
      return ESP_ERR_INVALID_SIZE;
    }
    bin_size = (uint32_t)fsz;
  }
  if (bin_size == 0) {
    ESP_LOGE(TAG, "Invalid image size");
    return ESP_ERR_INVALID_ARG;
  }

  block = malloc(OTA_BLOCK);
  if (block == NULL) {
    ESP_LOGE(TAG, "no memory for OTA block buffer");
    if (f != NULL)
      fclose(f);
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG,
           "C5 OTA: pushing %lu bytes (%s) over UART%d @ %d baud",
           (unsigned long)bin_size,
           (f != NULL) ? "SD image" : "caller image",
           OTA_UART,
           OTA_BAUD);

  uart_flush(OTA_UART);

  // Trigger the C5 to flip UART0 from console to raw OTA receive, over SPI. The
  // UART carries only the binary now - no UART magic. After the trigger the C5
  // silences its logging and replies OTA_READY on UART0, but a few console log
  // bytes may still be in flight before it goes quiet, so drain until READY
  // rather than reading a single byte.
  bool synced = false;
  for (int attempt = 1; attempt <= OTA_SYNC_ATTEMPTS && !synced; attempt++) {
    spi_header_t sresp = {0};
    esp_err_t tr = spi_bridge_send_command(
        SPI_ID_SYSTEM_START_UART_OTA, NULL, 0, &sresp, NULL, START_UART_OTA_TIMEOUT_MS);
    if (tr != ESP_OK) {
      ESP_LOGW(TAG,
               "start-uart-ota %d/%d: SPI cmd failed (%s)",
               attempt,
               OTA_SYNC_ATTEMPTS,
               esp_err_to_name(tr));
      continue;
    }
    int64_t deadline = esp_timer_get_time() + (int64_t)OTA_READY_TIMEOUT_MS * 1000;
    while (esp_timer_get_time() < deadline) {
      uint8_t r = 0;
      int n = uart_read_bytes(OTA_UART, &r, 1, pdMS_TO_TICKS(50));
      if (n == 1 && r == OTA_READY) {
        synced = true;
        ESP_LOGI(TAG, "C5 in OTA mode, READY (attempt %d/%d)", attempt, OTA_SYNC_ATTEMPTS);
        break;
      }
    }
    if (!synced) {
      ESP_LOGW(TAG,
               "start-uart-ota %d/%d: no READY within %d ms",
               attempt,
               OTA_SYNC_ATTEMPTS,
               OTA_READY_TIMEOUT_MS);
    }
  }
  if (!synced) {
    ESP_LOGE(TAG, "C5 OTA trigger failed after %d attempts", OTA_SYNC_ATTEMPTS);
    ESP_LOGE(TAG, "  the C5 must be RUNNING ITS APP (SPI bridge up) to accept the trigger");
    ESP_LOGE(TAG, "  a blank C5 will NOT respond -- use ROM flash or passthrough instead");
    goto cleanup;
  }
  ESP_LOGI(TAG, "C5 synced - sending image");

  uint8_t size_hdr[4];
  size_hdr[0] = (uint8_t)(bin_size & 0xFF);
  size_hdr[1] = (uint8_t)((bin_size >> 8) & 0xFF);
  size_hdr[2] = (uint8_t)((bin_size >> 16) & 0xFF);
  size_hdr[3] = (uint8_t)((bin_size >> 24) & 0xFF);
  uart_write_bytes(OTA_UART, (const char *)size_hdr, sizeof(size_hdr));
  uart_wait_tx_done(OTA_UART, pdMS_TO_TICKS(2000));

  uint8_t begin = 0;
  int bn = uart_read_bytes(OTA_UART, &begin, 1, pdMS_TO_TICKS(OTA_BEGIN_TIMEOUT_MS));
  if (bn != 1 || begin != OTA_ACK) {
    if (bn != 1)
      ESP_LOGE(TAG,
               "C5 not ready after begin: no reply within %d ms (erase too slow, or C5 hung)",
               OTA_BEGIN_TIMEOUT_MS);
    else
      ESP_LOGE(TAG, "C5 not ready after begin: got 0x%02X, want ACK 0x%02X", begin, OTA_ACK);
    goto cleanup;
  }
  ESP_LOGI(TAG,
           "C5 erased its OTA slot - streaming %lu bytes (block=%d)...",
           (unsigned long)bin_size,
           OTA_BLOCK);
  uint32_t off = 0;
  uint32_t t_stream0 = xTaskGetTickCount();
  s_ota_total = bin_size;
  s_ota_sent = 0;
  while (off < bin_size) {
    uint32_t chunk = (bin_size - off > OTA_BLOCK) ? OTA_BLOCK : bin_size - off;
    const uint8_t *src;
    if (f != NULL) {
      size_t got = fread(block, 1, chunk, f);
      if (got != chunk) {
        ESP_LOGE(TAG,
                 "SD read short @ %lu (got %u, want %lu)",
                 (unsigned long)off,
                 (unsigned)got,
                 (unsigned long)chunk);
        goto cleanup;
      }
      src = block;
    } else {
      src = bin_data + off;
    }
    int w = uart_write_bytes(OTA_UART, (const char *)src, chunk);
    if (w < 0) {
      ESP_LOGE(TAG, "uart_write_bytes failed @ %lu", (unsigned long)off);
      goto cleanup;
    }
    uart_wait_tx_done(OTA_UART, pdMS_TO_TICKS(2000));

    uint8_t r = 0;
    int n = uart_read_bytes(OTA_UART, &r, 1, pdMS_TO_TICKS(OTA_BLOCK_TIMEOUT_MS));
    if (n != 1 || r != OTA_ACK) {
      if (n == 1 && r == OTA_NAK) {
        ESP_LOGE(TAG, "C5 NAK at block @ %lu", (unsigned long)off);
      } else {
        ESP_LOGE(TAG, "no block ACK @ %lu (n=%d r=0x%02X)", (unsigned long)off, n, r);
      }
      goto cleanup;
    }

    off += chunk;
    s_ota_sent = off;
    if ((off & 0x3FFFF) < OTA_BLOCK || off == bin_size) {
      ESP_LOGI(TAG,
               "  sent %lu/%lu (%lu%%)",
               (unsigned long)off,
               (unsigned long)bin_size,
               (unsigned long)((uint64_t)off * 100 / bin_size));
    }
  }
  uint32_t stream_ms = pdTICKS_TO_MS(xTaskGetTickCount() - t_stream0);
  ESP_LOGI(TAG,
           "Image sent in %lu ms (%lu B/s) - waiting for C5 to verify and ACK...",
           (unsigned long)stream_ms,
           stream_ms ? (unsigned long)((uint64_t)bin_size * 1000 / stream_ms) : 0UL);

  uint8_t resp = 0;
  int rn = uart_read_bytes(OTA_UART, &resp, 1, pdMS_TO_TICKS(OTA_ACK_TIMEOUT_MS));
  if (rn == 1 && resp == OTA_ACK) {
    ESP_LOGI(TAG, "C5 ACK - OTA applied, C5 rebooting into new firmware");
    result = ESP_OK;
  } else if (rn == 1 && resp == OTA_NAK) {
    ESP_LOGE(TAG, "C5 NAK - OTA rejected (image invalid or transfer error)");
  } else {
    ESP_LOGE(TAG, "no ACK from C5 (n=%d resp=0x%02X)", rn, resp);
  }

cleanup:
  free(block);
  if (f != NULL)
    fclose(f);
  return result;
}
