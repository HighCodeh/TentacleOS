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

#define OTA_SPI_CHUNK         240  // firmware bytes per OTA_DATA command (fits the u8 length)
#define OTA_DATA_TIMEOUT_MS   300  // per-chunk OTA_DATA timeout; short so a lost ack recovers fast
#define OTA_CHUNK_DEADLINE_MS 5000 // total time to land one chunk (retries BUSY / lost acks)
#define OTA_MAX_BLOCK         4096 // block buffer (UART writes; SPI uses OTA_SPI_CHUNK of it)
#define OTA_UART_WINDOW       12288 // bytes the P4 may run ahead of the C5 (< its 16KB ring buffer)

#define C5_SD_FW_PATH "/sdcard/c5/TentacleOS_C5.bin"

#define OTA_SYNC_ATTEMPTS     10    // OTA_BEGIN command retries
#define OTA_BEGIN_TIMEOUT_MS  1000  // OTA_BEGIN command timeout
#define OTA_STATUS_TIMEOUT_MS 1000  // OTA_STATUS poll timeout
#define OTA_ERASE_TIMEOUT_MS  20000 // wait for the C5 to erase and reach READY
#define OTA_DONE_TIMEOUT_MS   30000 // final: wait for validate + DONE/ERROR

#define ENTER_DOWNLOAD_TIMEOUT_MS 500

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

// Poll the C5's OTA progress over SPI. Fills @p st on success.
static esp_err_t c5_ota_poll(spi_ota_status_t *st) {
  spi_header_t resp = {0};
  return spi_bridge_send_command(
      SPI_ID_SYSTEM_OTA_STATUS, NULL, 0, &resp, (uint8_t *)st, OTA_STATUS_TIMEOUT_MS);
}

esp_err_t c5_flasher_update(const uint8_t *bin_data, uint32_t bin_size, uint8_t transport) {
  FILE *f = NULL;
  uint8_t *block = NULL;
  esp_err_t result = ESP_FAIL;
  // NOTE: SPI_OTA_TRANSPORT_UART does NOT work on the HighBoy V2 - there is no
  // direct P4<->C5 UART on this board. The schematic wires the P4 UART0 and the
  // C5 UART0 to two separate CP2105 channels (they only meet over USB), so the
  // bytes we write on GPIO38 go to the P4's own USB serial, never to the C5, and
  // the C5's receiver stalls. It is kept for a future board that routes a real
  // P4->C5 UART; on V2 use SPI_OTA_TRANSPORT_SPI. See GPIO_C5_UART_* in pin_def.h.
  const bool uart = (transport == SPI_OTA_TRANSPORT_UART);

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

  block = malloc(OTA_MAX_BLOCK);
  if (block == NULL) {
    ESP_LOGE(TAG, "no memory for OTA block buffer");
    if (f != NULL)
      fclose(f);
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG,
           "C5 OTA: pushing %lu bytes (%s) over %s",
           (unsigned long)bin_size,
           (f != NULL) ? "SD image" : "caller image",
           uart ? "UART (control on SPI)" : "SPI");

  // Control is on SPI regardless of transport. Tell the C5 to begin (size +
  // transport); it erases the target partition, then receives the image over the
  // chosen transport. The BEGIN ack is easily lost (it coincides with the C5's
  // erase, which stalls the bridge), so confirm via STATUS that it actually
  // started rather than trusting the ack; a redundant BEGIN is harmless.
  const spi_ota_begin_t begin_req = {.size = bin_size, .transport = transport};
  spi_ota_status_t st = {0};
  bool started = false;
  for (int attempt = 1; attempt <= OTA_SYNC_ATTEMPTS && !started; attempt++) {
    spi_header_t sresp = {0};
    spi_bridge_send_command(SPI_ID_SYSTEM_OTA_BEGIN,
                            (const uint8_t *)&begin_req,
                            sizeof(begin_req),
                            &sresp,
                            NULL,
                            OTA_BEGIN_TIMEOUT_MS);
    if (c5_ota_poll(&st) == ESP_OK) {
      if (st.state == SPI_OTA_STATE_ERASING || st.state == SPI_OTA_STATE_READY ||
          st.state == SPI_OTA_STATE_RECEIVING) {
        started = true;
        break;
      }
      if (st.state == SPI_OTA_STATE_ERROR) {
        ESP_LOGE(TAG, "C5 reported ERROR on OTA begin");
        goto cleanup;
      }
    }
    ESP_LOGW(TAG, "start-ota %d/%d: C5 not started yet, retrying", attempt, OTA_SYNC_ATTEMPTS);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  if (!started) {
    ESP_LOGE(TAG, "C5 did not start the OTA after %d attempts", OTA_SYNC_ATTEMPTS);
    ESP_LOGE(TAG, "  the C5 must be RUNNING ITS APP (SPI bridge up) to accept it");
    goto cleanup;
  }

  // Wait for the C5 to finish erasing and reach READY - all over SPI.
  int64_t deadline = esp_timer_get_time() + (int64_t)OTA_ERASE_TIMEOUT_MS * 1000;
  bool ready = false;
  while (esp_timer_get_time() < deadline) {
    if (c5_ota_poll(&st) == ESP_OK) {
      if (st.state == SPI_OTA_STATE_READY) {
        ready = true;
        break;
      }
      if (st.state == SPI_OTA_STATE_ERROR) {
        ESP_LOGE(TAG, "C5 reported ERROR before streaming");
        goto cleanup;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  if (!ready) {
    ESP_LOGE(TAG, "C5 did not reach READY within %d ms (erase too slow or hung)",
             OTA_ERASE_TIMEOUT_MS);
    goto cleanup;
  }
  ESP_LOGI(TAG,
           "C5 erased its OTA slot - streaming %lu bytes over %s...",
           (unsigned long)bin_size,
           uart ? "UART" : "SPI");

  uint32_t off = 0;
  uint32_t t_stream0 = xTaskGetTickCount();
  s_ota_total = bin_size;
  s_ota_sent = 0;
  const uint32_t block_sz = uart ? OTA_MAX_BLOCK : OTA_SPI_CHUNK;
  while (off < bin_size) {
    uint32_t chunk = (bin_size - off > block_sz) ? block_sz : bin_size - off;
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

    if (uart) {
      // UART transport: write the block raw, then pace to the C5's committed
      // count so we never run past its ring buffer, and catch a reported ERROR.
      int w = uart_write_bytes(OTA_UART, (const char *)src, chunk);
      if (w < 0) {
        ESP_LOGE(TAG, "uart_write_bytes failed @ %lu", (unsigned long)off);
        goto cleanup;
      }
      uart_wait_tx_done(OTA_UART, pdMS_TO_TICKS(2000));
      off += chunk;
      int64_t bdl = esp_timer_get_time() + (int64_t)OTA_CHUNK_DEADLINE_MS * 1000;
      while (esp_timer_get_time() < bdl) {
        if (c5_ota_poll(&st) == ESP_OK) {
          if (st.state == SPI_OTA_STATE_ERROR) {
            ESP_LOGE(TAG, "C5 reported ERROR @ %lu", (unsigned long)off);
            goto cleanup;
          }
          if (off - st.bytes_written <= OTA_UART_WINDOW) {
            break; // enough headroom in the C5's ring buffer to keep going
          }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
      }
    } else {
      // SPI transport: OTA_DATA chunk. The C5 buffers it and acks at once (a
      // writer task does the flash). OK -> advance; BUSY -> writer behind, wait
      // and resend; TIMEOUT -> ack lost, ask bytes_written (off or off+chunk,
      // never partial/duplicate) and resend if needed; other -> fatal.
      spi_header_t dresp = {0};
      bool chunk_done = false;
      int64_t cdl = esp_timer_get_time() + (int64_t)OTA_CHUNK_DEADLINE_MS * 1000;
      while (!chunk_done && esp_timer_get_time() < cdl) {
        esp_err_t dr = spi_bridge_send_command(
            SPI_ID_SYSTEM_OTA_DATA, src, (uint8_t)chunk, &dresp, NULL, OTA_DATA_TIMEOUT_MS);
        if (dr == ESP_OK) {
          off += chunk;
          chunk_done = true;
        } else if (dr == ESP_ERR_INVALID_STATE) {
          vTaskDelay(pdMS_TO_TICKS(5)); // BUSY: let the writer drain, then resend
        } else if (dr == ESP_ERR_TIMEOUT) {
          if (c5_ota_poll(&st) == ESP_OK) {
            if (st.state == SPI_OTA_STATE_ERROR) {
              ESP_LOGE(TAG, "C5 reported ERROR @ %lu", (unsigned long)off);
              goto cleanup;
            }
            if (st.bytes_written >= off + chunk) {
              off += chunk;
              chunk_done = true;
            }
          }
        } else {
          ESP_LOGE(TAG, "C5 rejected chunk @ %lu (%s)", (unsigned long)off, esp_err_to_name(dr));
          goto cleanup;
        }
      }
      if (!chunk_done) {
        ESP_LOGE(
            TAG, "chunk @ %lu not acked within %d ms", (unsigned long)off, OTA_CHUNK_DEADLINE_MS);
        goto cleanup;
      }
    }

    s_ota_sent = off;
    if ((off & 0x3FFFF) < block_sz || off >= bin_size) {
      ESP_LOGI(TAG,
               "  sent %lu/%lu (%lu%%)",
               (unsigned long)off,
               (unsigned long)bin_size,
               (unsigned long)((uint64_t)off * 100 / bin_size));
    }
  }
  uint32_t stream_ms = pdTICKS_TO_MS(xTaskGetTickCount() - t_stream0);
  ESP_LOGI(TAG,
           "Image sent in %lu ms (%lu B/s) - waiting for C5 to verify...",
           (unsigned long)stream_ms,
           stream_ms ? (unsigned long)((uint64_t)bin_size * 1000 / stream_ms) : 0UL);

  // Final: wait for DONE (validated + set-boot) over SPI. The C5 reboots shortly
  // after DONE, so once it goes unresponsive with everything acked, treat it as OK.
  deadline = esp_timer_get_time() + (int64_t)OTA_DONE_TIMEOUT_MS * 1000;
  while (esp_timer_get_time() < deadline) {
    if (c5_ota_poll(&st) == ESP_OK) {
      if (st.state == SPI_OTA_STATE_DONE) {
        ESP_LOGI(TAG, "C5 DONE - OTA applied, C5 rebooting into new firmware");
        result = ESP_OK;
        break;
      }
      if (st.state == SPI_OTA_STATE_ERROR) {
        ESP_LOGE(TAG, "C5 ERROR - OTA rejected (image invalid or write failure)");
        break;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  if (result != ESP_OK && st.state != SPI_OTA_STATE_ERROR) {
    ESP_LOGE(TAG, "no DONE from C5 within %d ms (last state=%d)", OTA_DONE_TIMEOUT_MS, st.state);
  }

  if (result == ESP_OK) {
    // The C5 is rebooting into the new firmware; the current bridge link is stale.
    // Mark it down so the link monitor re-probes and reconnects the new C5.
    spi_bridge_set_alive(false);
    ESP_LOGI(TAG, "bridge marked down; run 'c5 sync' or wait for auto re-detect");
  }

cleanup:
  free(block);
  if (f != NULL)
    fclose(f);
  return result;
}

esp_err_t c5_flasher_ping(void) {
  spi_header_t resp = {0};
  return spi_bridge_send_command(SPI_ID_SYSTEM_PING, NULL, 0, &resp, NULL, OTA_STATUS_TIMEOUT_MS);
}

esp_err_t c5_flasher_info(void) {
  spi_sys_info_t info = {0};
  spi_header_t resp = {0};
  esp_err_t r = spi_bridge_send_command(
      SPI_ID_SYSTEM_INFO, NULL, 0, &resp, (uint8_t *)&info, OTA_STATUS_TIMEOUT_MS);
  if (r != ESP_OK) {
    return r;
  }
  printf("C5 info:\n");
  printf("  chip model : %u\n", info.chip_model);
  printf("  chip rev   : %u.%u\n", info.chip_revision / 100, info.chip_revision % 100);
  printf("  base MAC   : %02x:%02x:%02x:%02x:%02x:%02x\n",
         info.mac[0],
         info.mac[1],
         info.mac[2],
         info.mac[3],
         info.mac[4],
         info.mac[5]);
  printf("  free heap  : %lu bytes\n", (unsigned long)info.free_heap);
  return ESP_OK;
}

esp_err_t c5_flasher_sync(void) {
  // Allow one probe even if the bridge is currently marked down, then keep it
  // alive only if the C5 actually answers.
  spi_bridge_set_alive(true);
  esp_err_t r = c5_flasher_ping();
  spi_bridge_set_alive(r == ESP_OK);
  ESP_LOGI(TAG, "C5 sync: %s", (r == ESP_OK) ? "linked" : esp_err_to_name(r));
  return r;
}
