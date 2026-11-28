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
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cc1101.h"
#include "flipper_subghz.h"
#include "subghz_protocol_registry.h"
#include "subghz_protocol_serializer.h"
#include "subghz_receiver.h"
#include "subghz_storage.h"
#include "subghz_transmitter.h"
#include "subghz_types.h"

static const char *TAG = "SUBGHZ_REPLAY";

#define REPLAY_FILE_BUF    65536
#define REPLAY_MAX_PULSES  8192
#define REPLAY_RX_DRAIN_MS 1000
#define REPLAY_RX_POLL_MS  10

static void copy_field(const char *content, const char *field, char *out, size_t size) {
  size_t length = strlen(field);
  for (const char *line = content; line != NULL && *line;) {
    if (strncmp(line, field, length) == 0) {
      line += length;
      size_t count = strcspn(line, "\r\n");
      if (count >= size)
        count = size - 1;
      memcpy(out, line, count);
      out[count] = '\0';
      return;
    }
    line = strchr(line, '\n');
    if (line != NULL)
      ++line;
  }
}

esp_err_t subghz_replay_load(const char *name, subghz_replay_t *out_replay) {
  if (name == NULL || out_replay == NULL)
    return ESP_ERR_INVALID_ARG;

  *out_replay = (subghz_replay_t){0};
  subghz_raw_file_t *file = NULL;
  esp_err_t raw_error = subghz_raw_file_load(name, &file);
  if (raw_error == ESP_OK) {
    subghz_replay_from_raw(file, out_replay);
    subghz_raw_file_release(file);
    return ESP_OK;
  }
  if (raw_error != ESP_ERR_NOT_SUPPORTED)
    return raw_error;
  char *content = heap_caps_malloc(REPLAY_FILE_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
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
  esp_err_t error = subghz_replay_prepare(content, out_replay);
  free(content);
  return error;
}

esp_err_t subghz_replay_prepare(const char *content, subghz_replay_t *out_replay) {
  if (content == NULL || out_replay == NULL)
    return ESP_ERR_INVALID_ARG;
  *out_replay = (subghz_replay_t){0};
  int32_t *pulses =
      heap_caps_malloc(REPLAY_MAX_PULSES * sizeof(*pulses), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
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
  uint32_t repeats = 0;
  err = subghz_protocol_parse_repeat(content, &repeats);
  if (err != ESP_OK)
    goto cleanup;
  if (count > REPLAY_MAX_PULSES / repeats) {
    err = ESP_ERR_INVALID_SIZE;
    goto cleanup;
  }
  // Keep frame repeats contiguous within one PTT transmission.
  for (uint32_t repeat = 1; repeat < repeats; ++repeat)
    memcpy(pulses + repeat * count, pulses, count * sizeof(*pulses));
  count *= repeats;
  subghz_tx_config_t config = {
      .frequency_hz = frequency, .preset = (cc1101_preset_t)preset, .has_radio_config = true};
  err = subghz_protocol_parse_radio(content, config.preset, &config.radio);
  if (err != ESP_OK)
    goto cleanup;
  out_replay->pulses = pulses;
  out_replay->count = count;
  out_replay->config = config;
  copy_field(content, "Protocol: ", out_replay->protocol, sizeof(out_replay->protocol));
  copy_field(content, "Key: ", out_replay->key, sizeof(out_replay->key));
  if (strcmp(out_replay->protocol, "RAW") != 0) {
    out_replay->details =
        heap_caps_malloc(SUBGHZ_REPLAY_DETAILS_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (out_replay->details == NULL) {
      ESP_LOGE(TAG, "Cannot allocate protocol details");
    } else if (flipper_subghz_describe(content, out_replay->details, SUBGHZ_REPLAY_DETAILS_SIZE) !=
               ESP_OK) {
      free(out_replay->details);
      out_replay->details = NULL;
      ESP_LOGW(TAG, "Protocol details unavailable; using file summary");
    }
  }
  pulses = NULL;
cleanup:
  free(pulses);
  return err;
}

void subghz_replay_free(subghz_replay_t *replay) {
  if (replay == NULL)
    return;
  subghz_raw_file_release(replay->raw_file);
  free(replay->pulses);
  free(replay->details);
  *replay = (subghz_replay_t){0};
}

esp_err_t subghz_replay_send(const subghz_replay_t *replay) {
  if (replay == NULL || (!replay->raw_file && (replay->pulses == NULL || replay->count == 0)))
    return ESP_ERR_INVALID_ARG;
  if (subghz_receiver_is_running()) {
    subghz_receiver_stop();
    for (uint32_t elapsed = 0; elapsed < REPLAY_RX_DRAIN_MS && subghz_receiver_is_running();
         elapsed += REPLAY_RX_POLL_MS)
      vTaskDelay(pdMS_TO_TICKS(REPLAY_RX_POLL_MS));
    if (subghz_receiver_is_running())
      return ESP_ERR_TIMEOUT;
  }
  esp_err_t error = subghz_tx_init();
  if (error != ESP_OK)
    return error;
  if (replay->raw_file)
    return subghz_tx_send_file(replay->raw_file, &replay->config);
  return subghz_tx_send_configured(replay->pulses, replay->count, &replay->config);
}

esp_err_t subghz_replay_file(const char *name) {
  subghz_replay_t replay = {0};
  esp_err_t error = subghz_replay_load(name, &replay);
  if (error == ESP_OK)
    error = subghz_replay_send(&replay);
  subghz_replay_free(&replay);
  return error;
}

void subghz_replay_from_raw(subghz_raw_file_t *file, subghz_replay_t *out_replay) {
  *out_replay = (subghz_replay_t){.raw_file = file};
  subghz_raw_file_acquire(file);
  uint64_t count;
  subghz_raw_file_info(file, &out_replay->config.frequency_hz, &out_replay->config.radio, &count);
  out_replay->count = count > SIZE_MAX ? SIZE_MAX : count;
  out_replay->config.preset = out_replay->config.radio.preset;
  out_replay->config.has_radio_config = true;
  snprintf(out_replay->protocol, sizeof(out_replay->protocol), "RAW");
}
