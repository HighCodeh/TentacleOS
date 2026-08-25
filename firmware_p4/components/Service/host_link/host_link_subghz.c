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

#include "host_link_subghz.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "host_link.h"
#include "spi_protocol.h"
#include "subghz_receiver.h"
#include "subghz_replay.h"
#include "subghz_spectrum.h"
#include "subghz_storage.h"
#include "subghz_transmitter.h"
#include "sys_prio.h"

#define SUBGHZ_TX_MAX_TIMINGS 512
#define SUBGHZ_NAME_MAX       48
#define SUBGHZ_POLL_MS        30

static TaskHandle_t s_rx_task = NULL;
static TaskHandle_t s_spec_task = NULL;
static volatile bool s_rx_run = false;
static volatile bool s_spec_run = false;
static int32_t s_tx_timings[SUBGHZ_TX_MAX_TIMINGS];
static bool s_storage_ready = false;

static void ensure_storage(void) {
  if (!s_storage_ready) {
    subghz_storage_init();
    s_storage_ready = true;
  }
}

static void rx_poll_task(void *arg) {
  (void)arg;
  subghz_rx_result_t res;
  uint32_t last_seq = 0;
  while (s_rx_run) {
    if (subghz_receiver_get_result(&res) && res.seq != 0 && res.seq != last_seq) {
      last_seq = res.seq;
      uint8_t buf[64];
      uint16_t n = 0;
      memcpy(buf + n, &res.seq, 4);
      n += 4;
      buf[n++] = res.decoded ? 1 : 0;
      memcpy(buf + n, &res.freq, 4);
      n += 4;
      buf[n++] = res.data.bit_count;
      buf[n++] = res.data.btn;
      memcpy(buf + n, &res.data.serial, 4);
      n += 4;
      memcpy(buf + n, &res.data.raw_value, 4);
      n += 4;
      const char *proto = res.data.protocol_name ? res.data.protocol_name : "";
      uint8_t plen = (uint8_t)strnlen(proto, 24);
      buf[n++] = plen;
      memcpy(buf + n, proto, plen);
      n += plen;
      host_link_emit_stream(SPI_CAT_SUBGHZ, SPI_ID_SUBGHZ_RX_FRAME & 0xFF, buf, n);
    }
    vTaskDelay(pdMS_TO_TICKS(SUBGHZ_POLL_MS));
  }
  subghz_receiver_stop();
  s_rx_task = NULL;
  vTaskDelete(NULL);
}

static void spec_poll_task(void *arg) {
  (void)arg;
  subghz_spectrum_line_t line;
  while (s_spec_run) {
    if (subghz_spectrum_get_line(&line)) {
      uint8_t buf[17 + SPECTRUM_SAMPLES];
      uint16_t n = 0;
      memcpy(buf + n, &line.center_freq, 4);
      n += 4;
      memcpy(buf + n, &line.span_hz, 4);
      n += 4;
      memcpy(buf + n, &line.start_freq, 4);
      n += 4;
      memcpy(buf + n, &line.step_hz, 4);
      n += 4;
      buf[n++] = SPECTRUM_SAMPLES;
      for (int i = 0; i < SPECTRUM_SAMPLES; i++)
        buf[n++] = (int8_t)line.dbm_values[i];
      host_link_emit_stream(SPI_CAT_SUBGHZ, SPI_ID_SUBGHZ_SPECTRUM_LINE & 0xFF, buf, n);
    }
    vTaskDelay(pdMS_TO_TICKS(SUBGHZ_POLL_MS));
  }
  subghz_spectrum_stop();
  s_spec_task = NULL;
  vTaskDelete(NULL);
}

bool host_subghz_is_op(uint16_t cmd) {
  return (cmd >> 8) == SPI_CAT_SUBGHZ;
}

void host_subghz_stop(void) {
  s_rx_run = false;
  s_spec_run = false;
  for (int i = 0; i < 20 && (s_rx_task != NULL || s_spec_task != NULL); i++)
    vTaskDelay(pdMS_TO_TICKS(20));
}

uint8_t host_subghz_handle(uint16_t cmd,
                           const uint8_t *payload,
                           uint16_t plen,
                           uint8_t *out,
                           uint16_t out_cap,
                           uint16_t *out_len) {
  *out_len = 0;

  switch (cmd) {
    case SPI_ID_SUBGHZ_RX_START: {
      if (plen < 6)
        return SPI_STATUS_INVALID_ARG;
      if (s_rx_task != NULL || s_spec_task != NULL)
        return SPI_STATUS_BUSY;
      subghz_mode_t mode = (subghz_mode_t)payload[0];
      cc1101_preset_t preset = (cc1101_preset_t)payload[1];
      uint32_t freq = (uint32_t)(payload[2] | (payload[3] << 8) | (payload[4] << 16) |
                                 (payload[5] << 24));
      if (subghz_receiver_start(mode, preset, freq) != ESP_OK)
        return SPI_STATUS_ERROR;
      s_rx_run = true;
      if (xTaskCreatePinnedToCore(rx_poll_task, "sg_rx", 4096, NULL, SYS_PRIO_SERVICE_LO,
                                  &s_rx_task, SYS_CORE_RADIO) != pdPASS) {
        s_rx_run = false;
        subghz_receiver_stop();
        return SPI_STATUS_ERROR;
      }
      return SPI_STATUS_OK;
    }

    case SPI_ID_SUBGHZ_RX_STOP:
      s_rx_run = false;
      for (int i = 0; i < 20 && s_rx_task != NULL; i++)
        vTaskDelay(pdMS_TO_TICKS(20));
      return SPI_STATUS_OK;

    case SPI_ID_SUBGHZ_TX_RAW: {
      if (plen < 2)
        return SPI_STATUS_INVALID_ARG;
      uint16_t count = (uint16_t)(payload[0] | (payload[1] << 8));
      if (count == 0 || count > SUBGHZ_TX_MAX_TIMINGS)
        return SPI_STATUS_INVALID_ARG;
      if (plen < (uint16_t)(2 + count * sizeof(int32_t)))
        return SPI_STATUS_INVALID_ARG;
      memcpy(s_tx_timings, payload + 2, count * sizeof(int32_t));
      if (subghz_tx_init() != ESP_OK)
        return SPI_STATUS_ERROR;
      esp_err_t r = subghz_tx_send_raw(s_tx_timings, count);
      subghz_tx_stop();
      return (r == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    case SPI_ID_SUBGHZ_REPLAY: {
      if (plen == 0)
        return SPI_STATUS_INVALID_ARG;
      char name[SUBGHZ_NAME_MAX] = {0};
      uint16_t copy = plen < SUBGHZ_NAME_MAX ? plen : SUBGHZ_NAME_MAX - 1;
      memcpy(name, payload, copy);
      ensure_storage();
      return (subghz_replay_file(name) == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    case SPI_ID_SUBGHZ_SPECTRUM_START: {
      if (plen < 8)
        return SPI_STATUS_INVALID_ARG;
      if (s_rx_task != NULL || s_spec_task != NULL)
        return SPI_STATUS_BUSY;
      uint32_t center = (uint32_t)(payload[0] | (payload[1] << 8) | (payload[2] << 16) |
                                   (payload[3] << 24));
      uint32_t span = (uint32_t)(payload[4] | (payload[5] << 8) | (payload[6] << 16) |
                                 (payload[7] << 24));
      subghz_spectrum_start(center, span);
      s_spec_run = true;
      if (xTaskCreatePinnedToCore(spec_poll_task, "sg_spec", 4096, NULL, SYS_PRIO_SERVICE_LO,
                                  &s_spec_task, SYS_CORE_RADIO) != pdPASS) {
        s_spec_run = false;
        subghz_spectrum_stop();
        return SPI_STATUS_ERROR;
      }
      return SPI_STATUS_OK;
    }

    case SPI_ID_SUBGHZ_SPECTRUM_STOP:
      s_spec_run = false;
      for (int i = 0; i < 20 && s_spec_task != NULL; i++)
        vTaskDelay(pdMS_TO_TICKS(20));
      return SPI_STATUS_OK;

    case SPI_ID_SUBGHZ_LIST: {
      ensure_storage();
      // Entry per row: [name 40][protocol 24][freq u32] = 68 bytes. P4-local ops
      // have no data pipe, so pack [count u8] + rows inline into the response.
      const uint16_t row = SUBGHZ_STORAGE_NAME_MAX + SUBGHZ_STORAGE_PROTO_MAX + 4;
      int max_rows = (out_cap - 1) / row;
      if (max_rows > 32)
        max_rows = 32;
      static subghz_storage_entry_t entries[32];
      int count = subghz_storage_list(entries, max_rows);
      if (count < 0)
        count = 0;
      uint16_t n = 0;
      out[n++] = (uint8_t)count;
      for (int i = 0; i < count; i++) {
        memcpy(out + n, entries[i].name, SUBGHZ_STORAGE_NAME_MAX);
        n += SUBGHZ_STORAGE_NAME_MAX;
        memcpy(out + n, entries[i].protocol, SUBGHZ_STORAGE_PROTO_MAX);
        n += SUBGHZ_STORAGE_PROTO_MAX;
        memcpy(out + n, &entries[i].frequency, 4);
        n += 4;
      }
      *out_len = n;
      return SPI_STATUS_OK;
    }

    case SPI_ID_SUBGHZ_DELETE: {
      if (plen == 0)
        return SPI_STATUS_INVALID_ARG;
      char name[SUBGHZ_NAME_MAX] = {0};
      uint16_t copy = plen < SUBGHZ_NAME_MAX ? plen : SUBGHZ_NAME_MAX - 1;
      memcpy(name, payload, copy);
      ensure_storage();
      return (subghz_storage_delete(name) == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
    }

    default:
      return SPI_STATUS_UNSUPPORTED;
  }
}
