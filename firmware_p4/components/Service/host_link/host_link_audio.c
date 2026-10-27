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

#include "host_link_audio.h"

#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio_i2s.h"
#include "spi_protocol.h"
#include "sys_prio.h"

typedef struct {
  float freq_hz;
  int dur_ms;
  float amp;
} tone_req_t;

// play_tone blocks for dur_ms; run it on a transient task so the host link never
// stalls waiting for a tone to finish.
static void tone_task(void *arg) {
  tone_req_t *req = (tone_req_t *)arg;
  audio_i2s_play_tone(req->freq_hz, req->dur_ms, req->amp);
  free(req);
  vTaskDelete(NULL);
}

uint8_t host_audio_handle(uint16_t cmd,
                          const uint8_t *payload,
                          uint16_t plen,
                          uint8_t *out,
                          uint16_t out_cap,
                          uint16_t *out_len) {
  (void)out;
  (void)out_cap;
  *out_len = 0;

  switch (cmd) {
    case SPI_ID_AUDIO_SET_VOLUME:
      if (plen < 1)
        return SPI_STATUS_INVALID_ARG;
      audio_i2s_set_volume(payload[0]);
      return SPI_STATUS_OK;

    case SPI_ID_AUDIO_TONE: {
      if (plen < 5)
        return SPI_STATUS_INVALID_ARG;
      tone_req_t *req = (tone_req_t *)malloc(sizeof(tone_req_t));
      if (req == NULL)
        return SPI_STATUS_ERROR;
      req->freq_hz = (float)(payload[0] | (payload[1] << 8));
      req->dur_ms = (int)(payload[2] | (payload[3] << 8));
      req->amp = payload[4] / 100.0f;
      if (req->amp > 1.0f)
        req->amp = 1.0f;
      if (xTaskCreatePinnedToCore(
              tone_task, "hl_tone", 3072, req, SYS_PRIO_SERVICE_LO, NULL, SYS_CORE_UI) != pdPASS) {
        free(req);
        return SPI_STATUS_ERROR;
      }
      return SPI_STATUS_OK;
    }

    case SPI_ID_AUDIO_CHIME:
      audio_play_chime();
      return SPI_STATUS_OK;

    case SPI_ID_AUDIO_CLICK:
      audio_click();
      return SPI_STATUS_OK;

    default:
      return SPI_STATUS_UNSUPPORTED;
  }
}
