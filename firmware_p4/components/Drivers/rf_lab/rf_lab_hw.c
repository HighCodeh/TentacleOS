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

#include "rf_lab_hw.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "board_pins.h"
#include "cc1101.h"
#include "cc1101_lease.h"
#include "cc1101_rf_path.h"
#include "hb_nfc_spi.h"
#include "lf_rfid.h"
#include "pin_def.h"
#include "spi.h"
#include "st25r3916_reg.h"
#include "sx1262_hal.h"
#include "sx1262_cmd.h"
#include "sx1262_regs.h"

#define STATUS_READ       0x40U
#define CC_STATE_MASK     0x1FU
#define CC_STATE_IDLE     1U
#define STATE_ATTEMPTS    20U
#define STATE_DELAY_MS    5U
#define NFC_CLOCK_HZ      1000000U
#define SX_MODE_MASK      0x70U
#define SX_STANDBY_STATUS 0x20U
static const char *TAG = "RF_LAB_HW";

static esp_err_t cc_sleep(void) {
  esp_err_t error = cc1101_lease_acquire();
  if (error != ESP_OK)
    return error;
  cc1101_init();
  error = cc1101_is_initialized() ? ESP_OK : ESP_ERR_INVALID_STATE;
  if (error == ESP_OK) {
    uint8_t version = cc1101_read_reg(CC1101_VERSION | STATUS_READ);
    if (version == 0 || version == UINT8_MAX)
      error = ESP_ERR_NOT_FOUND;
  }
  if (error == ESP_OK) {
    cc1101_strobe(CC1101_SIDLE);
    error = ESP_ERR_TIMEOUT;
    for (unsigned i = 0; i < STATE_ATTEMPTS; i++) {
      if ((cc1101_read_reg(CC1101_MARCSTATE | STATUS_READ) & CC_STATE_MASK) == CC_STATE_IDLE) {
        error = ESP_OK;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(STATE_DELAY_MS));
    }
  }
  if (error == ESP_OK)
    cc1101_strobe(CC1101_SPWD);
  esp_err_t spi_error = cc1101_lease_take_error();
  esp_err_t release = cc1101_lease_release();
  if (error == ESP_OK)
    error = spi_error;
  return error == ESP_OK ? release : error;
}
static esp_err_t sx_sleep(void) {
  sx1262_hal_t hal = {0};
  esp_err_t error = sx1262_hal_create(&hal);
  if (error != ESP_OK)
    return error;
  uint8_t wake[] = {SX1262_OP_GET_STATUS, 0};
  uint8_t response[2] = {0};
  hal.lock(hal.ctx);
  hal.cs_low(hal.ctx);
  hal.delay_ms(hal.ctx, 1);
  int transferred = hal.spi_transfer(hal.ctx, wake, response, sizeof(wake));
  hal.cs_high(hal.ctx);
  hal.unlock(hal.ctx);
  hal.delay_ms(hal.ctx, 1);
  error = transferred < 0 ? ESP_FAIL : sx1262_cmd_wait_busy(&hal, 0);
  uint8_t parameter = SX1262_STDBY_RC;
  if (error == ESP_OK)
    error = sx1262_cmd_write(&hal, SX1262_OP_SET_STANDBY, &parameter, 1);
  if (error == ESP_OK)
    error = sx1262_cmd_wait_busy(&hal, 0);
  if (error == ESP_OK) {
    // GetStatus returns status on the second SPI byte, unlike register reads.
    hal.lock(hal.ctx);
    hal.cs_low(hal.ctx);
    transferred = hal.spi_transfer(hal.ctx, wake, response, sizeof(wake));
    hal.cs_high(hal.ctx);
    hal.unlock(hal.ctx);
    if (transferred < 0)
      error = ESP_FAIL;
    else if ((response[1] & SX_MODE_MASK) != SX_STANDBY_STATUS)
      error = ESP_ERR_INVALID_RESPONSE;
  }
  parameter = SX1262_SLEEP_COLD_START;
  if (error == ESP_OK)
    error = sx1262_cmd_write(&hal, SX1262_OP_SET_SLEEP, &parameter, 1);
  hal.delay_ms(hal.ctx, STATE_DELAY_MS);
  esp_err_t close = sx1262_hal_destroy();
  return error == ESP_OK ? close : error;
}
static esp_err_t nfc_sleep(void) {
  hb_nfc_spi_config_t config = {.spi_host = SPI3_HOST,
                                .pin_mosi = GPIO_SPI_MOSI_PIN,
                                .pin_miso = GPIO_SPI_MISO_PIN,
                                .pin_sclk = GPIO_SPI_SCLK_PIN,
                                .pin_cs = GPIO_NFC_CS_PIN,
                                .mode = 1,
                                .clock_hz = NFC_CLOCK_HZ};
  esp_err_t error = hb_nfc_spi_init(&config);
  if (error != ESP_OK)
    return error;
  error = hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, 0);
  uint8_t value = UINT8_MAX, identity = 0;
  if (error == ESP_OK)
    error = hb_nfc_spi_reg_read(ST25R3916_REG_OP_CTRL, &value);
  if (error == ESP_OK)
    error = hb_nfc_spi_reg_read(ST25R3916_REG_IC_IDENTITY, &identity);
  if (error == ESP_OK && (value != 0 || identity == 0 || identity == UINT8_MAX))
    error = ESP_ERR_INVALID_RESPONSE;
  esp_err_t close = hb_nfc_spi_deinit();
  return error == ESP_OK ? close : error;
}
void rf_lab_hw_quiet(rf_lab_hw_status_t *out_status) {
  if (out_status == NULL)
    return;
  out_status->c5 = board_pins_hold_c5(true);
  out_status->lf = lf_rfid_init();
  out_status->nfc = nfc_sleep();
  out_status->cc = cc_sleep();
  out_status->sx = sx_sleep();
  ESP_LOGI(TAG,
           "quiet C5=%s LF=%s NFC=%s CC=%s SX=%s",
           esp_err_to_name(out_status->c5),
           esp_err_to_name(out_status->lf),
           esp_err_to_name(out_status->nfc),
           esp_err_to_name(out_status->cc),
           esp_err_to_name(out_status->sx));
}
esp_err_t rf_lab_hw_path_set(uint8_t path) {
  uint32_t v1, v2;
  if (!cc1101_rf_path_levels(path, &v1, &v2))
    return ESP_ERR_INVALID_ARG;
  const gpio_config_t config = {.pin_bit_mask =
                                    (1ULL << GPIO_RF_SW_V1_PIN) | (1ULL << GPIO_RF_SW_V2_PIN),
                                .mode = GPIO_MODE_INPUT_OUTPUT,
                                .pull_up_en = GPIO_PULLUP_DISABLE,
                                .pull_down_en = GPIO_PULLDOWN_DISABLE,
                                .intr_type = GPIO_INTR_DISABLE};
  esp_err_t error = gpio_config(&config);
  if (error == ESP_OK)
    error = gpio_set_level(GPIO_RF_SW_V1_PIN, v1);
  if (error == ESP_OK)
    error = gpio_set_level(GPIO_RF_SW_V2_PIN, v2);
  uint8_t actual = 0;
  if (error == ESP_OK)
    error = rf_lab_hw_path_get(&actual);
  if (error == ESP_OK && actual != path)
    error = ESP_ERR_INVALID_RESPONSE;
  return error;
}
esp_err_t rf_lab_hw_path_get(uint8_t *out_path) {
  if (out_path == NULL)
    return ESP_ERR_INVALID_ARG;
  *out_path = 1 + (gpio_get_level(GPIO_RF_SW_V1_PIN) << 1) + gpio_get_level(GPIO_RF_SW_V2_PIN);
  return ESP_OK;
}
