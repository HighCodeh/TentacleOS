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

#include "rf_chamber.h"
#include <string.h>
#include "esp_log.h"
#define MIN_DURATION_MS  100U
#define MIN_INTERVAL_MS  250U
#define MAX_INTERVAL_MS  60000U
#define HIGH_POWER_DBM   14
#define HIGH_POWER_CW_MS 1000U
#define SEQUENCE_OFFSET  4U
#define SEQUENCE_BYTES   4U
#define PAYLOAD_PATTERN  0xA5U
static const char *TAG __attribute__((unused)) = "RF_CHAMBER_CONFIG";

esp_err_t rf_chamber_validate(const rf_chamber_config_t *config) {
  if (config == NULL || config->radio < 0 || config->radio >= RF_CHAMBER_RADIO_COUNT ||
      config->mode < 0 || config->mode >= RF_CHAMBER_MODE_COUNT ||
      config->duration_ms < MIN_DURATION_MS || config->duration_ms > RF_CHAMBER_MAX_MS ||
      config->interval_ms < MIN_INTERVAL_MS || config->interval_ms > MAX_INTERVAL_MS ||
      (config->mode == RF_CHAMBER_CW && config->duration_ms > RF_CHAMBER_MAX_CW_MS))
    return ESP_ERR_INVALID_ARG;
  if (config->radio == RF_CHAMBER_CC) {
    uint32_t hz = config->frequency_hz;
    if (!((hz >= 300000000U && hz <= 348000000U) || (hz >= 387000000U && hz <= 464000000U) ||
          (hz >= 779000000U && hz <= 928000000U)) ||
        config->path < 1 || config->path > 4 || config->sf != 7 || config->bandwidth_khz != 125 ||
        config->cr != 1)
      return ESP_ERR_INVALID_ARG;
    static const int8_t LEVELS[] = {-30, -20, -10, 0, 5, 10};
    for (size_t i = 0; i < sizeof(LEVELS) / sizeof(LEVELS[0]); ++i)
      if (config->power_dbm == LEVELS[i])
        return ESP_OK;
    return ESP_ERR_INVALID_ARG;
  }
  if (config->frequency_hz < 863000000U || config->frequency_hz > 928000000U || config->path != 0 ||
      config->sf < 7 || config->sf > 12 ||
      (config->bandwidth_khz != 125 && config->bandwidth_khz != 250 &&
       config->bandwidth_khz != 500) ||
      config->cr < 1 || config->cr > 4 || config->power_dbm < -9 || config->power_dbm > 22 ||
      (config->mode == RF_CHAMBER_CW && config->power_dbm > HIGH_POWER_DBM &&
       config->duration_ms > HIGH_POWER_CW_MS))
    return ESP_ERR_INVALID_ARG;
  return ESP_OK;
}
void rf_chamber_payload(uint32_t sequence, uint8_t *out_payload) {
  if (out_payload == NULL)
    return;
  memcpy(out_payload, "HBRF", SEQUENCE_OFFSET);
  for (uint8_t i = 0; i < SEQUENCE_BYTES; ++i)
    out_payload[SEQUENCE_OFFSET + i] = sequence >> ((SEQUENCE_BYTES - 1 - i) * 8);
  for (uint8_t i = SEQUENCE_OFFSET + SEQUENCE_BYTES; i < RF_CHAMBER_PAYLOAD_SIZE; ++i)
    out_payload[i] = PAYLOAD_PATTERN ^ i;
}
bool rf_chamber_parse_payload(const uint8_t *data, uint8_t length, uint32_t *out_sequence) {
  if (data == NULL || out_sequence == NULL || length != RF_CHAMBER_PAYLOAD_SIZE ||
      memcmp(data, "HBRF", SEQUENCE_OFFSET) != 0)
    return false;
  for (uint8_t i = SEQUENCE_OFFSET + SEQUENCE_BYTES; i < RF_CHAMBER_PAYLOAD_SIZE; ++i)
    if (data[i] != (PAYLOAD_PATTERN ^ i))
      return false;
  uint32_t sequence = 0;
  for (uint8_t i = 0; i < SEQUENCE_BYTES; ++i)
    sequence = (sequence << 8) | data[SEQUENCE_OFFSET + i];
  *out_sequence = sequence;
  return true;
}
