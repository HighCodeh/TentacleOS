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

#include "highboy_nfc.h"

#include "hb_nfc_spi.h"
#include "hb_nfc_timer.h"
#include "st25r3916_cmd.h"
#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "rf_frontend.h"

static bool s_initialized;

esp_err_t highboy_nfc_init(const highboy_nfc_config_t *config) {
  if (config == NULL)
    return ESP_ERR_INVALID_ARG;
  if (config->spi_mode != 1 || config->spi_clock_hz == 0 || config->spi_clock_hz > 10000000U)
    return ESP_ERR_INVALID_ARG;
  bool is_reserved = rf_frontend_reserve(RF_FRONTEND_NFC_13MHZ) == ESP_OK;
  if (!is_reserved && !rf_frontend_can_access(RF_FRONTEND_NFC_13MHZ))
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = ESP_OK;
  if (!s_initialized) {
    error = st25r3916_core_init(config);
    if (error == ESP_OK)
      s_initialized = true;
  }
  if (is_reserved) {
    esp_err_t release_error = rf_frontend_unreserve(RF_FRONTEND_NFC_13MHZ);
    if (release_error != ESP_OK)
      error = release_error;
  }
  return error;
}

void highboy_nfc_deinit(void) {
  if (!s_initialized) {
    return;
  }
  if (st25r3916_core_deinit() == ESP_OK) {
    s_initialized = false;
  }
}

bool highboy_nfc_is_initialized(void) {
  return s_initialized;
}

esp_err_t highboy_nfc_ping(uint8_t *out_chip_id) {
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  uint8_t id = 0;
  esp_err_t err = st25r3916_core_check_id(&id, NULL, NULL);
  if (err == ESP_OK && out_chip_id != NULL) {
    *out_chip_id = id;
  }
  return err;
}

esp_err_t highboy_nfc_field_on(void) {
  return st25r3916_core_field_on();
}

esp_err_t highboy_nfc_field_off(void) {
  return st25r3916_core_field_off();
}

uint8_t highboy_nfc_measure_amplitude(void) {
  if (!s_initialized) {
    return 0;
  }
  uint8_t amplitude = 0;
  if (hb_nfc_spi_direct_cmd(ST25R3916_CMD_MEAS_AMPLITUDE) != ESP_OK) {
    return 0;
  }
  hb_nfc_timer_delay_ms(ST25R3916_AD_MEAS_DELAY_MS);
  if (hb_nfc_spi_reg_read(ST25R3916_REG_AD_RESULT, &amplitude) != ESP_OK) {
    return 0;
  }
  return amplitude;
}

bool highboy_nfc_field_detected(uint8_t *out_aux_display) {
  if (!s_initialized) {
    return false;
  }
  uint8_t aux = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux) != ESP_OK) {
    return false;
  }
  if (out_aux_display != NULL) {
    *out_aux_display = aux;
  }
  return (aux & ST25R3916_AUX_DISP_EFD_BIT) != 0;
}

esp_err_t highboy_nfc_set_regulator_mv(uint16_t millivolts) {
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  if (millivolts < 2400U || millivolts > 3400U || (millivolts % 100U) != 0U) {
    return ESP_ERR_INVALID_ARG;
  }

  /* Table 92: in 3.3 V mode code 5 is 2.4 V and code 15 is 3.4 V. */
  uint8_t code = (uint8_t)((millivolts - 1900U) / 100U);
  uint8_t ctrl = (uint8_t)(ST25R3916_REGULATOR_REG_S | (code << 3));
  esp_err_t err = st25r3916_core_field_off();
  if (err != ESP_OK) {
    return err;
  }
  return hb_nfc_spi_reg_write(ST25R3916_REG_REGULATOR_CTRL, ctrl);
}

esp_err_t highboy_nfc_adjust_regulator_auto(void) {
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  esp_err_t err = st25r3916_core_field_off();
  if (err != ESP_OK) {
    return err;
  }
  err = hb_nfc_spi_reg_modify(
      ST25R3916_REG_REGULATOR_CTRL, ST25R3916_REGULATOR_REG_S, ST25R3916_REGULATOR_REG_S);
  if (err == ESP_OK) {
    err = hb_nfc_spi_reg_modify(ST25R3916_REG_REGULATOR_CTRL, ST25R3916_REGULATOR_REG_S, 0x00);
  }
  if (err == ESP_OK) {
    err = hb_nfc_spi_direct_cmd(ST25R3916_CMD_ADJUST_REGULATORS);
  }
  hb_nfc_timer_delay_ms(ST25R3916_REGULATOR_DELAY_MS);
  return err;
}

esp_err_t highboy_nfc_get_regulator(uint16_t *out_millivolts,
                                    bool *out_current_limit,
                                    bool *out_manual,
                                    uint8_t *out_raw_display) {
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  uint8_t display = 0;
  uint8_t ctrl = 0;
  esp_err_t err = hb_nfc_spi_reg_read_b(ST25R3916_REG_B_REGULATOR_DISPLAY, &display);
  if (err == ESP_OK) {
    err = hb_nfc_spi_reg_read(ST25R3916_REG_REGULATOR_CTRL, &ctrl);
  }
  if (err != ESP_OK) {
    return err;
  }

  uint8_t code = (uint8_t)(display >> 4);
  if (out_millivolts != NULL) {
    *out_millivolts = code >= 5U ? (uint16_t)(1900U + ((uint16_t)code * 100U)) : 0U;
  }
  if (out_current_limit != NULL) {
    *out_current_limit = (display & 0x01U) != 0U;
  }
  if (out_manual != NULL) {
    *out_manual = (ctrl & ST25R3916_REGULATOR_REG_S) != 0U;
  }
  if (out_raw_display != NULL) {
    *out_raw_display = display;
  }
  return ESP_OK;
}
