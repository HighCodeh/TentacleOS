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

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "sys_prio.h"

static const char *TAG = "OTA_SVC";

// Plausible C5 app image size bounds - guards against acting on a bogus size.
#define OTA_MIN_SIZE 0x10000
#define OTA_MAX_SIZE 0x200000

// SPI transport: a RAM buffer decouples the OTA_DATA acks from the flash writes -
// the bridge task only pushes chunks here (fast, no flash) and acks at once,
// while the writer task drains this and does the slow esp_ota_write.
#define OTA_STREAM_SIZE   (16 * 1024)
#define OTA_WRITE_BUF     1024
#define OTA_RECV_STALL_MS 5000 // writer gives up if the source stops feeding it

// UART transport: UART0 (U0RXD=GPIO12, U0TXD=GPIO11) carries the raw .bin from the
// P4; control (begin/status) stays on SPI so the C5 never transmits on UART here.
#define OTA_UART        UART_NUM_0
#define OTA_UART_RX_PIN 12
#define OTA_UART_TX_PIN 11
#define OTA_UART_BAUD   115200
#define OTA_UART_RINGBUF (16 * 1024)

static volatile uint8_t s_state = SPI_OTA_STATE_IDLE;
static volatile uint32_t s_bytes_written = 0;
static uint32_t s_ota_size = 0;
static uint8_t s_transport = SPI_OTA_TRANSPORT_SPI;
static StreamBufferHandle_t s_stream = NULL;

static void ota_reboot_task(void *arg) {
  (void)arg;
  vTaskDelay(pdMS_TO_TICKS(500)); // let the P4 poll DONE before we drop the link
  esp_restart();
}

// Bring UART0 up for RX. Console output on U0TXD (GPIO11) is untouched; the .bin
// only comes in on U0RXD (GPIO12).
static esp_err_t ota_uart_open(void) {
  const uart_config_t cfg = {
      .baud_rate = OTA_UART_BAUD,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  if (!uart_is_driver_installed(OTA_UART)) {
    esp_err_t err = uart_driver_install(OTA_UART, OTA_UART_RINGBUF, 0, 0, NULL, 0);
    if (err != ESP_OK) {
      return err;
    }
  }
  esp_err_t err = uart_param_config(OTA_UART, &cfg);
  if (err == ESP_OK) {
    err = uart_set_pin(
        OTA_UART, OTA_UART_TX_PIN, OTA_UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  }
  return err;
}

static void ota_writer_task(void *arg) {
  (void)arg;

  const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
  if (part == NULL) {
    ESP_LOGE(TAG, "no OTA partition available");
    s_state = SPI_OTA_STATE_ERROR;
    vTaskDelete(NULL);
    return;
  }

  const bool uart = (s_transport == SPI_OTA_TRANSPORT_UART);
  if (uart && ota_uart_open() != ESP_OK) {
    ESP_LOGE(TAG, "failed to open UART0 for OTA");
    s_state = SPI_OTA_STATE_ERROR;
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGW(TAG, "OTA: %lu bytes -> '%s' via %s (erasing...)", (unsigned long)s_ota_size, part->label,
           uart ? "UART" : "SPI");
  esp_ota_handle_t handle = 0;
  s_state = SPI_OTA_STATE_ERASING;
  esp_err_t err = esp_ota_begin(part, s_ota_size, &handle); // erases (multi-second)
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
    s_state = SPI_OTA_STATE_ERROR;
    goto fail;
  }

  if (uart) {
    uart_flush_input(OTA_UART);
  }
  s_state = SPI_OTA_STATE_READY; // the P4 polls for this before sending data
  ESP_LOGI(TAG, "erased - ready for %lu bytes", (unsigned long)s_ota_size);

  static uint8_t buf[OTA_WRITE_BUF];
  while (s_bytes_written < s_ota_size) {
    uint32_t remaining = s_ota_size - s_bytes_written;
    size_t want = remaining < sizeof(buf) ? remaining : sizeof(buf);
    size_t n;
    if (uart) {
      int r = uart_read_bytes(OTA_UART, buf, want, pdMS_TO_TICKS(OTA_RECV_STALL_MS));
      n = (r > 0) ? (size_t)r : 0;
    } else {
      n = xStreamBufferReceive(s_stream, buf, want, pdMS_TO_TICKS(OTA_RECV_STALL_MS));
    }
    if (n == 0) {
      ESP_LOGE(TAG, "stall @ %lu/%lu", (unsigned long)s_bytes_written, (unsigned long)s_ota_size);
      esp_ota_abort(handle);
      s_state = SPI_OTA_STATE_ERROR;
      goto fail;
    }
    err = esp_ota_write(handle, buf, n);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_ota_write @ %lu: %s", (unsigned long)s_bytes_written, esp_err_to_name(err));
      esp_ota_abort(handle);
      s_state = SPI_OTA_STATE_ERROR;
      goto fail;
    }
    s_bytes_written += n;
    s_state = SPI_OTA_STATE_RECEIVING;
    if ((s_bytes_written % (256 * 1024)) < n) {
      ESP_LOGI(TAG, "  written %lu/%lu", (unsigned long)s_bytes_written, (unsigned long)s_ota_size);
    }
  }

  ESP_LOGI(TAG, "all %lu bytes written, validating...", (unsigned long)s_ota_size);
  err = esp_ota_end(handle);
  if (err == ESP_OK) {
    err = esp_ota_set_boot_partition(part);
  }
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "finalize failed: %s", esp_err_to_name(err));
    s_state = SPI_OTA_STATE_ERROR;
    goto fail;
  }

  ESP_LOGW(TAG, "OTA OK - booting '%s'", part->label);
  s_state = SPI_OTA_STATE_DONE;
  if (uart && uart_is_driver_installed(OTA_UART)) {
    uart_driver_delete(OTA_UART);
  }
  xTaskCreate(ota_reboot_task, "ota_reboot", 2048, NULL, SYS_PRIO_SERVICE_HI, NULL);
  vTaskDelete(NULL);
  return;

fail:
  if (uart && uart_is_driver_installed(OTA_UART)) {
    uart_driver_delete(OTA_UART);
  }
  vTaskDelete(NULL);
}

esp_err_t ota_service_begin(uint32_t size, uint8_t transport) {
  if (s_state == SPI_OTA_STATE_ERASING || s_state == SPI_OTA_STATE_READY ||
      s_state == SPI_OTA_STATE_RECEIVING) {
    return ESP_ERR_INVALID_STATE; // one already running
  }
  if (size < OTA_MIN_SIZE || size > OTA_MAX_SIZE) {
    ESP_LOGE(TAG, "implausible OTA size %lu", (unsigned long)size);
    s_state = SPI_OTA_STATE_ERROR;
    return ESP_ERR_INVALID_SIZE;
  }
  if (transport != SPI_OTA_TRANSPORT_SPI && transport != SPI_OTA_TRANSPORT_UART) {
    return ESP_ERR_INVALID_ARG;
  }

  if (transport == SPI_OTA_TRANSPORT_SPI) {
    if (s_stream == NULL) {
      s_stream = xStreamBufferCreate(OTA_STREAM_SIZE, 1);
      if (s_stream == NULL) {
        ESP_LOGE(TAG, "failed to create OTA stream buffer");
        s_state = SPI_OTA_STATE_ERROR;
        return ESP_ERR_NO_MEM;
      }
    } else {
      xStreamBufferReset(s_stream);
    }
  }

  s_ota_size = size;
  s_bytes_written = 0;
  s_transport = transport;
  s_state = SPI_OTA_STATE_ERASING; // visible to the P4's first status poll

  if (xTaskCreate(ota_writer_task, "ota_wr", 6144, NULL, SYS_PRIO_SERVICE_HI, NULL) != pdPASS) {
    ESP_LOGE(TAG, "failed to create writer task");
    s_state = SPI_OTA_STATE_ERROR;
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

esp_err_t ota_service_write(const uint8_t *data, uint16_t len) {
  if (s_transport != SPI_OTA_TRANSPORT_SPI || s_stream == NULL ||
      (s_state != SPI_OTA_STATE_READY && s_state != SPI_OTA_STATE_RECEIVING &&
       s_state != SPI_OTA_STATE_ERASING)) {
    return ESP_ERR_INVALID_STATE; // -> SPI_STATUS_ERROR (fatal for the P4)
  }
  if (len == 0) {
    return ESP_OK;
  }
  if (xStreamBufferSpacesAvailable(s_stream) < len) {
    return ESP_ERR_NO_MEM; // -> SPI_STATUS_BUSY: writer is behind, P4 retries
  }
  xStreamBufferSend(s_stream, data, len, 0); // never blocks: space was checked
  return ESP_OK;
}

void ota_service_get_status(spi_ota_status_t *out) {
  if (out == NULL) {
    return;
  }
  out->state = s_state;
  out->bytes_written = s_bytes_written;
}
