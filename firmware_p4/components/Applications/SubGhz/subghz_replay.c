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

#include "subghz_replay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cc1101.h"
#include "subghz_protocol_registry.h"
#include "subghz_protocol_serializer.h"
#include "subghz_receiver.h"
#include "subghz_storage.h"
#include "subghz_transmitter.h"
#include "subghz_types.h"

static const char *TAG = "SUBGHZ_REPLAY";

#define REPLAY_FILE_BUF    8192
#define REPLAY_MAX_PULSES  2048
#define REPLAY_RX_DRAIN_MS 150

esp_err_t subghz_replay_file(const char *name) {
  if (name == NULL)
    return ESP_ERR_INVALID_ARG;

  char *content = malloc(REPLAY_FILE_BUF);
  if (content == NULL) {
    ESP_LOGE(TAG, "Cannot allocate replay text");
    return ESP_ERR_NO_MEM;
  }

  int rd = subghz_storage_read(name, content, REPLAY_FILE_BUF);
  if (rd <= 0) {
    free(content);
    return ESP_ERR_NOT_FOUND;
  }

  if (rd >= REPLAY_FILE_BUF - 1 || memchr(content, '\0', (size_t)rd) != NULL) {
    free(content);
    return ESP_ERR_INVALID_SIZE;
  }
  int32_t *pulses = malloc(REPLAY_MAX_PULSES * sizeof(*pulses));
  esp_err_t err = ESP_ERR_NO_MEM;
  if (pulses == NULL) {
    ESP_LOGE(TAG, "Cannot allocate replay pulses");
    goto cleanup;
  }
  size_t count = 0;
  uint32_t frequency = 0;
  uint8_t preset = 0;
  err = subghz_protocol_parse_capture(
      content, pulses, REPLAY_MAX_PULSES, &count, &frequency, &preset);
  if (err != ESP_OK)
    goto cleanup;
  if (subghz_receiver_is_running()) {
    subghz_receiver_stop();
    vTaskDelay(pdMS_TO_TICKS(REPLAY_RX_DRAIN_MS));
  }
  err = subghz_tx_init();
  if (err != ESP_OK)
    goto cleanup;
  if (frequency != 0)
    cc1101_set_frequency(frequency);
  err = subghz_tx_send_raw(pulses, count);
cleanup:
  free(pulses);
  free(content);
  return err;
}
