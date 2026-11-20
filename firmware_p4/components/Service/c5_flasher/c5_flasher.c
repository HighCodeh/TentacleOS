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
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_pins.h"
#include "pin_def.h"
#include "spi_session.h"
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

#define OTA_SPI_CHUNK         240  // firmware bytes per OTA_DATA command (fits the u8 length)
#define OTA_DATA_TIMEOUT_MS   300  // per-chunk OTA_DATA timeout; short so a lost ack recovers fast
#define OTA_CHUNK_DEADLINE_MS 5000 // total time to land one chunk (retries BUSY / lost acks)
#define OTA_MAX_BLOCK         4096 // SD read buffer for SPI OTA

#define C5_SD_FW_PATH "/sdcard/c5/TentacleOS_C5.bin"

#define OTA_SYNC_ATTEMPTS     10    // OTA_BEGIN command retries
#define OTA_BEGIN_TIMEOUT_MS  1000  // OTA_BEGIN command timeout
#define OTA_STATUS_TIMEOUT_MS 1000  // OTA_STATUS poll timeout
#define OTA_ERASE_TIMEOUT_MS  20000 // wait for the C5 to erase and reach READY
#define OTA_DONE_TIMEOUT_MS   30000 // final: wait for validate + DONE/ERROR

#define ENTER_DOWNLOAD_TIMEOUT_MS 500
#define C5_REBOOT_TIMEOUT_MS      2000

esp_err_t c5_flasher_init(void) {
  return ESP_OK; // SPI transport needs no UART GPIO setup.
}

esp_err_t c5_flasher_enter_download(void) {
  esp_err_t guard = spi_bridge_maintenance_begin();
  if (guard != ESP_OK)
    return guard;
  ESP_LOGW(TAG, "Requesting C5 to enter ROM download mode over SPI...");

  spi_header_t resp = {0};
  esp_err_t ret = spi_bridge_send_command(
      SPI_ID_SYSTEM_ENTER_DOWNLOAD, NULL, 0, &resp, NULL, 0, ENTER_DOWNLOAD_TIMEOUT_MS);
  if (ret == ESP_OK || ret == ESP_ERR_TIMEOUT) {
    spi_bridge_recovery_disable();
    spi_bridge_maintenance_end();
    vTaskDelay(pdMS_TO_TICKS(300));
    return ESP_OK;
  }
  spi_bridge_maintenance_end();
  ESP_LOGE(TAG, "enter-download command failed: %s", esp_err_to_name(ret));
  return ret;
}

esp_err_t c5_flasher_enter_download_physical(void) {
  esp_err_t ret = spi_bridge_recovery_begin(ENTER_DOWNLOAD_TIMEOUT_MS);
  if (ret != ESP_OK)
    return ret;
  spi_bridge_recovery_disable();
  spi_session_link_lost();
  spi_bridge_suspend(true);
  ret = board_pins_download_c5();
  spi_bridge_recovery_finish(false);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "C5 BOOT asserted and reset pulsed; use its CP2105 port, then power-cycle");
  }
  return ret;
}

esp_err_t c5_flasher_reboot(void) {
  esp_err_t guard = spi_bridge_maintenance_begin();
  if (guard != ESP_OK)
    return guard;
  ESP_LOGW(TAG, "Requesting C5 reboot over SPI...");

  spi_header_t resp = {0};
  esp_err_t ret =
      spi_bridge_send_command(SPI_ID_SYSTEM_REBOOT, NULL, 0, &resp, NULL, 0, C5_REBOOT_TIMEOUT_MS);
  spi_bridge_maintenance_end();
  if (ret == ESP_OK || ret == ESP_ERR_TIMEOUT) {
    return ESP_OK;
  }
  ESP_LOGE(TAG, "reboot command failed: %s", esp_err_to_name(ret));
  return ret;
}

void c5_flasher_release_uart(void) {
  ESP_LOGI(TAG, "C5 UART is independently connected to CP2105");
}

// Poll the C5's OTA progress over SPI. Fills @p st on success.
static esp_err_t c5_ota_poll(spi_ota_status_t *st) {
  spi_header_t resp = {0};
  return spi_bridge_send_command(
      SPI_ID_SYSTEM_OTA_STATUS, NULL, 0, &resp, (uint8_t *)st, sizeof(*st), OTA_STATUS_TIMEOUT_MS);
}

esp_err_t c5_flasher_update(const uint8_t *bin_data, uint32_t bin_size, uint8_t transport) {
  FILE *f = NULL;
  uint8_t *block = NULL;
  bool ota_attempted = false;
  bool ota_terminal = false;
  esp_err_t result = ESP_FAIL;
  if (transport != SPI_OTA_TRANSPORT_SPI)
    return ESP_ERR_NOT_SUPPORTED;
  result = spi_bridge_maintenance_begin();
  if (result != ESP_OK)
    return result;
  result = ESP_FAIL;

  if (bin_data == NULL) {
    f = fopen(C5_SD_FW_PATH, "rb");
    if (f == NULL) {
      ESP_LOGE(TAG, "C5 firmware not on SD: %s (is the card inserted?)", C5_SD_FW_PATH);
      result = ESP_ERR_NOT_FOUND;
      goto cleanup;
    }
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz <= 0) {
      ESP_LOGE(TAG, "C5 firmware on SD is empty: %s", C5_SD_FW_PATH);
      result = ESP_ERR_INVALID_SIZE;
      goto cleanup;
    }
    bin_size = (uint32_t)fsz;
  }
  if (bin_size == 0) {
    ESP_LOGE(TAG, "Invalid image size");
    result = ESP_ERR_INVALID_ARG;
    goto cleanup;
  }

  block = malloc(OTA_MAX_BLOCK);
  if (block == NULL) {
    ESP_LOGE(TAG, "no memory for OTA block buffer");
    result = ESP_ERR_NO_MEM;
    goto cleanup;
  }

  ESP_LOGI(TAG,
           "C5 OTA: pushing %lu bytes (%s) over %s",
           (unsigned long)bin_size,
           (f != NULL) ? "SD image" : "caller image",
           "SPI");

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
    ota_attempted = true;
    spi_bridge_send_command(SPI_ID_SYSTEM_OTA_BEGIN,
                            (const uint8_t *)&begin_req,
                            sizeof(begin_req),
                            &sresp,
                            NULL,
                            0,
                            OTA_BEGIN_TIMEOUT_MS);
    if (c5_ota_poll(&st) == ESP_OK) {
      if (st.state == SPI_OTA_STATE_ERASING || st.state == SPI_OTA_STATE_READY ||
          st.state == SPI_OTA_STATE_RECEIVING) {
        started = true;
        break;
      }
      if (st.state == SPI_OTA_STATE_ERROR) {
        ota_terminal = true;
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
        ota_terminal = true;
        ESP_LOGE(TAG, "C5 reported ERROR before streaming");
        goto cleanup;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }
  if (!ready) {
    ESP_LOGE(
        TAG, "C5 did not reach READY within %d ms (erase too slow or hung)", OTA_ERASE_TIMEOUT_MS);
    goto cleanup;
  }
  ESP_LOGI(TAG,
           "C5 erased its OTA slot - streaming %lu bytes over %s...",
           (unsigned long)bin_size,
           "SPI");

  uint32_t off = 0;
  uint32_t t_stream0 = xTaskGetTickCount();
  s_ota_total = bin_size;
  s_ota_sent = 0;
  const uint32_t block_sz = OTA_SPI_CHUNK;
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

    {
      // SPI transport: OTA_DATA chunk. The C5 buffers it and acks at once (a
      // writer task does the flash). OK -> advance; BUSY -> writer behind, wait
      // and resend; TIMEOUT -> ack lost, ask bytes_written (off or off+chunk,
      // never partial/duplicate) and resend if needed; other -> fatal.
      spi_header_t dresp = {0};
      bool chunk_done = false;
      int64_t cdl = esp_timer_get_time() + (int64_t)OTA_CHUNK_DEADLINE_MS * 1000;
      while (!chunk_done && esp_timer_get_time() < cdl) {
        esp_err_t dr = spi_bridge_send_command(
            SPI_ID_SYSTEM_OTA_DATA, src, (uint8_t)chunk, &dresp, NULL, 0, OTA_DATA_TIMEOUT_MS);
        if (dr == ESP_OK) {
          off += chunk;
          chunk_done = true;
        } else if (dr == ESP_ERR_INVALID_STATE) {
          vTaskDelay(pdMS_TO_TICKS(5)); // BUSY: let the writer drain, then resend
        } else if (dr == ESP_ERR_TIMEOUT) {
          if (c5_ota_poll(&st) == ESP_OK) {
            if (st.state == SPI_OTA_STATE_ERROR) {
              ota_terminal = true;
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
        ota_terminal = true;
        ESP_LOGI(TAG, "C5 DONE - OTA applied, C5 rebooting into new firmware");
        result = ESP_OK;
        break;
      }
      if (st.state == SPI_OTA_STATE_ERROR) {
        ota_terminal = true;
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
  if (ota_attempted && !ota_terminal) {
    spi_bridge_recovery_disable();
    ESP_LOGW(TAG, "C5 OTA state unknown; automatic reset disabled until P4 restart");
  }
  spi_bridge_maintenance_end();
  free(block);
  if (f != NULL)
    fclose(f);
  return result;
}

esp_err_t c5_flasher_ping(void) {
  spi_header_t resp = {0};
  return spi_bridge_send_command(
      SPI_ID_SYSTEM_PING, NULL, 0, &resp, NULL, 0, OTA_STATUS_TIMEOUT_MS);
}

esp_err_t c5_flasher_info(void) {
  spi_sys_info_t info = {0};
  spi_header_t resp = {0};
  esp_err_t r = spi_bridge_send_command(
      SPI_ID_SYSTEM_INFO, NULL, 0, &resp, (uint8_t *)&info, sizeof(info), OTA_STATUS_TIMEOUT_MS);
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
