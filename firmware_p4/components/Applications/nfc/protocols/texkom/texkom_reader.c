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

#include "texkom.h"

#include <stdbool.h>

#include "esp_log.h"

#include "hb_nfc_spi.h"
#include "st25r3916_reg.h"

#define TEXKOM_MODE_STREAM 0x70U
#define TEXKOM_RX_FILTERS  0x13U
#define TEXKOM_RX_AGC      0x2DU
#define TEXKOM_RX_GAIN     0x60U
#define TEXKOM_CORRELATOR  0x13U

static const char *TAG = "TEXKOM_READER";

typedef struct {
  uint8_t address;
  uint8_t value;
  bool is_space_b;
} texkom_register_t;

esp_err_t texkom_read(texkom_data_t *out_data) {
  if (out_data == NULL)
    return ESP_ERR_INVALID_ARG;
  static const texkom_register_t CONFIG[] = {
      {ST25R3916_REG_MODE, TEXKOM_MODE_STREAM, false},
      {ST25R3916_REG_STREAM_MODE, 0, false},
      {ST25R3916_REG_RX_CONF1, TEXKOM_RX_FILTERS, false},
      {ST25R3916_REG_RX_CONF2, TEXKOM_RX_AGC, false},
      {ST25R3916_REG_RX_CONF3, TEXKOM_RX_GAIN, false},
      {ST25R3916_REG_RX_CONF4, 0, false},
      {ST25R3916_REG_B_CORR_CONF1, TEXKOM_CORRELATOR, true},
      {ST25R3916_REG_B_CORR_CONF2, 0, true},
  };
  uint8_t previous[sizeof(CONFIG) / sizeof(CONFIG[0])];
  size_t saved = 0;
  esp_err_t err = ESP_OK;
  for (size_t i = 0; i < sizeof(CONFIG) / sizeof(CONFIG[0]); i++) {
    const texkom_register_t *reg = &CONFIG[i];
    err = reg->is_space_b ? hb_nfc_spi_reg_read_b(reg->address, &previous[i])
                          : hb_nfc_spi_reg_read(reg->address, &previous[i]);
    if (err != ESP_OK)
      goto cleanup;
    saved++;
    err = reg->is_space_b ? hb_nfc_spi_reg_write_b(reg->address, reg->value)
                          : hb_nfc_spi_reg_write(reg->address, reg->value);
    if (err != ESP_OK)
      goto cleanup;
  }
  uint16_t gaps[TEXKOM_MAX_GAPS];
  size_t count = 0;
  err = hb_nfc_spi_capture_gaps(gaps, TEXKOM_MAX_GAPS, &count);
  if (err == ESP_OK)
    err = texkom_decode(gaps, count, out_data);
cleanup:
  while (saved > 0) {
    size_t i = --saved;
    const texkom_register_t *reg = &CONFIG[i];
    esp_err_t restore = reg->is_space_b ? hb_nfc_spi_reg_write_b(reg->address, previous[i])
                                        : hb_nfc_spi_reg_write(reg->address, previous[i]);
    if (restore != ESP_OK) {
      ESP_LOGE(TAG, "Receive configuration restore failed");
      err = restore;
    }
  }
  return err;
}

esp_err_t texkom_emulate_frame(const texkom_data_t *data) {
  uint16_t cycles[TEXKOM_MAX_GAPS + 1];
  size_t count = sizeof(cycles) / sizeof(cycles[0]);
  esp_err_t err = texkom_encode(data, cycles, &count);
  if (err != ESP_OK)
    return err;
  return hb_nfc_spi_target_pulses(cycles, count);
}
