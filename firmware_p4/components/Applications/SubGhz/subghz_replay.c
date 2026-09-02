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
#define REPLAY_PROTO_MAX   24

static uint32_t parse_u32_after(const char *content, const char *key) {
  const char *p = strstr(content, key);
  if (p == NULL)
    return 0;
  return (uint32_t)strtoul(p + strlen(key), NULL, 10);
}

static void parse_protocol(const char *content, char *out, size_t out_size) {
  out[0] = '\0';
  const char *p = strstr(content, "Protocol: ");
  if (p == NULL)
    return;
  p += strlen("Protocol: ");
  size_t i = 0;
  while (p[i] != '\0' && p[i] != '\n' && p[i] != '\r' && i < out_size - 1) {
    out[i] = p[i];
    i++;
  }
  out[i] = '\0';
}

static uint32_t parse_key_value(const char *content) {
  const char *p = strstr(content, "Key: ");
  if (p == NULL)
    return 0;
  unsigned b[8] = {0};
  int n = sscanf(p + strlen("Key: "),
                 "%x %x %x %x %x %x %x %x",
                 &b[0],
                 &b[1],
                 &b[2],
                 &b[3],
                 &b[4],
                 &b[5],
                 &b[6],
                 &b[7]);
  if (n < 8)
    return 0;
  return ((uint32_t)b[4] << 24) | ((uint32_t)b[5] << 16) | ((uint32_t)b[6] << 8) | (uint32_t)b[7];
}

static esp_err_t replay_raw(const char *content) {
  int32_t *pulses = malloc(REPLAY_MAX_PULSES * sizeof(int32_t));
  if (pulses == NULL)
    return ESP_ERR_NO_MEM;

  uint32_t freq = 0;
  uint8_t preset = 0;
  size_t n = subghz_protocol_parse_raw(content, pulses, REPLAY_MAX_PULSES, &freq, &preset);
  if (n == 0) {
    free(pulses);
    return ESP_FAIL;
  }

  if (freq != 0)
    cc1101_set_frequency(freq);
  esp_err_t err = subghz_tx_send_raw(pulses, n);
  free(pulses);
  return err;
}

static esp_err_t replay_decoded(const char *content) {
  uint32_t freq = parse_u32_after(content, "Frequency: ");
  char proto[REPLAY_PROTO_MAX];
  parse_protocol(content, proto, sizeof(proto));

  subghz_data_t data = {0};
  data.protocol_name = proto;
  data.bit_count = (uint8_t)parse_u32_after(content, "Bit: ");
  data.raw_value = parse_key_value(content);
  data.serial =
      (strstr(content, "Serial: ") != NULL) ? parse_u32_after(content, "Serial: ") : data.raw_value;
  data.btn = (uint8_t)parse_u32_after(content, "Btn: ");

  int32_t *pulses = malloc(REPLAY_MAX_PULSES * sizeof(int32_t));
  if (pulses == NULL)
    return ESP_ERR_NO_MEM;

  size_t n = subghz_protocol_registry_encode(proto, &data, pulses, REPLAY_MAX_PULSES);
  if (n == 0) {
    free(pulses);
    ESP_LOGW(TAG, "replay unsupported for protocol '%s'", proto);
    return ESP_ERR_NOT_SUPPORTED;
  }

  if (freq != 0)
    cc1101_set_frequency(freq);
  esp_err_t err = subghz_tx_send_raw(pulses, n);
  free(pulses);
  return err;
}

esp_err_t subghz_replay_file(const char *name) {
  if (name == NULL)
    return ESP_ERR_INVALID_ARG;

  char *content = malloc(REPLAY_FILE_BUF);
  if (content == NULL)
    return ESP_ERR_NO_MEM;

  int rd = subghz_storage_read(name, content, REPLAY_FILE_BUF);
  if (rd <= 0) {
    free(content);
    return ESP_ERR_NOT_FOUND;
  }

  if (subghz_receiver_is_running()) {
    subghz_receiver_stop();
    vTaskDelay(pdMS_TO_TICKS(REPLAY_RX_DRAIN_MS));
  }

  esp_err_t err = subghz_tx_init();
  if (err != ESP_OK) {
    free(content);
    return err;
  }

  bool is_raw = (strstr(content, "RAW_Data:") != NULL);
  err = is_raw ? replay_raw(content) : replay_decoded(content);

  free(content);
  return err;
}
