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

#include "st25r3916_core.h"

#include <string.h>

#include "esp_log.h"

#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"
#include "st25r3916_aat.h"
#include "hb_nfc_spi.h"
#include "hb_nfc_gpio.h"
#include "hb_nfc_timer.h"

static const char *TAG = "ST25R3916_CORE";

typedef struct {
  bool is_init;
  bool is_field_on;
  highboy_nfc_config_t config;
} st25r3916_core_drv_state_t;

static st25r3916_core_drv_state_t s_drv = {0};

esp_err_t st25r3916_core_init(const highboy_nfc_config_t *config) {
  if (config == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  s_drv.config = *config;

  if (hb_nfc_gpio_init(config->pin_irq) != ESP_OK) {
    return ESP_FAIL;
  }

  hb_nfc_spi_config_t spi_cfg = {
      .spi_host = config->spi_host,
      .pin_mosi = config->pin_mosi,
      .pin_miso = config->pin_miso,
      .pin_sclk = config->pin_sclk,
      .pin_cs = config->pin_cs,
      .mode = config->spi_mode,
      .clock_hz = config->spi_clock_hz,
  };
  if (hb_nfc_spi_init(&spi_cfg) != ESP_OK) {
    hb_nfc_gpio_deinit();
    return ESP_FAIL;
  }

  hb_nfc_timer_delay_ms(ST25R3916_SPI_INIT_DELAY_MS);
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_SET_DEFAULT);

  hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, ST25R3916_OP_CTRL_EN);

  bool osc_ok = false;
  for (int i = 0; i < ST25R3916_OSC_POLL_MAX; i++) {
    uint8_t irq_status = 0;
    hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT, &irq_status);
    if (irq_status & ST25R3916_IRQ_MAIN_OSC) {
      osc_ok = true;
      break;
    }
    hb_nfc_timer_delay_us(ST25R3916_OSC_POLL_INTERVAL_US);
  }

  if (!osc_ok) {
    ESP_LOGW(TAG, "OSC ready timeout");
  }

  uint8_t id, type, rev;
  if (st25r3916_core_check_id(&id, &type, &rev) != ESP_OK) {
    hb_nfc_spi_deinit();
    hb_nfc_gpio_deinit();
    return ESP_FAIL;
  }

  hb_nfc_spi_direct_cmd(ST25R3916_CMD_ADJUST_REGULATORS);
  hb_nfc_timer_delay_ms(ST25R3916_REGULATOR_DELAY_MS);
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_CALIBRATE_DRIVER);
  hb_nfc_timer_delay_ms(ST25R3916_DRIVER_CAL_DELAY_MS);

  s_drv.is_init = true;
  ESP_LOGI(TAG, "Init OK (Chip 0x%02X)", id);
  return ESP_OK;
}

void st25r3916_core_deinit(void) {
  if (!s_drv.is_init) {
    return;
  }
  st25r3916_core_field_off();
  hb_nfc_spi_deinit();
  hb_nfc_gpio_deinit();
  s_drv.is_init = false;
  s_drv.is_field_on = false;
}

esp_err_t st25r3916_core_check_id(uint8_t *out_id, uint8_t *out_type, uint8_t *out_rev) {
  uint8_t val = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_IC_IDENTITY, &val) != ESP_OK) {
    return ESP_FAIL;
  }

  if (val == 0x00 || val == 0xFF) {
    ESP_LOGE(TAG, "Hardware communication error (0x%02X)", val);
    return ESP_ERR_NOT_FOUND;
  }

  uint8_t ic_type = (val >> ST25R3916_IC_TYPE_SHIFT) & 0x1F;
  uint8_t ic_rev = val & ST25R3916_IC_REV_MASK;

  if (out_id != NULL)
    *out_id = val;
  if (out_type != NULL)
    *out_type = ic_type;
  if (out_rev != NULL)
    *out_rev = ic_rev;

  return ESP_OK;
}

esp_err_t st25r3916_core_field_on(void) {
  if (!s_drv.is_init) {
    return ESP_ERR_INVALID_STATE;
  }
  if (s_drv.is_field_on) {
    return ESP_OK;
  }

  hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, ST25R3916_OP_CTRL_FIELD_ON);
  hb_nfc_timer_delay_ms(ST25R3916_FIELD_ON_DELAY_MS);
  hb_nfc_spi_direct_cmd(ST25R3916_CMD_RESET_RX_GAIN);

  s_drv.is_field_on = true;
  return ESP_OK;
}

esp_err_t st25r3916_core_field_off(void) {
  if (!s_drv.is_init) {
    return ESP_OK;
  }
  hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, ST25R3916_OP_CTRL_FIELD_OFF);
  s_drv.is_field_on = false;
  return ESP_OK;
}

bool st25r3916_core_field_is_on(void) {
  return s_drv.is_field_on;
}

esp_err_t st25r3916_core_field_cycle(void) {
  if (!s_drv.is_init) {
    return ESP_ERR_INVALID_STATE;
  }

  st25r3916_core_field_off();
  hb_nfc_timer_delay_ms(ST25R3916_FIELD_CYCLE_DELAY_MS);
  return st25r3916_core_field_on();
}

esp_err_t st25r3916_core_set_mode_nfca(void) {
  if (!s_drv.is_init) {
    return ESP_ERR_INVALID_STATE;
  }

  hb_nfc_spi_reg_write(ST25R3916_REG_MODE, ST25R3916_MODE_POLL_NFCA);
  hb_nfc_spi_reg_write(ST25R3916_REG_BIT_RATE, 0x00);

  uint8_t iso_config = 0;
  hb_nfc_spi_reg_read(ST25R3916_REG_ISO14443A, &iso_config);
  iso_config &= (uint8_t)~(ST25R3916_ISO14443A_NO_TX_PAR | ST25R3916_ISO14443A_ANTCL);
  hb_nfc_spi_reg_write(ST25R3916_REG_ISO14443A, iso_config);

  return ESP_OK;
}

void st25r3916_core_dump_regs(void) {
  if (!s_drv.is_init) {
    return;
  }

  uint8_t regs[ST25R3916_REG_DUMP_COUNT];
  for (int i = 0; i < ST25R3916_REG_DUMP_COUNT; i++) {
    hb_nfc_spi_reg_read((uint8_t)i, &regs[i]);
  }

  for (int r = 0; r < ST25R3916_REG_DUMP_COUNT; r += ST25R3916_REG_DUMP_STRIDE) {
    ESP_LOGI(TAG,
             "Dump %02X: %02X %02X %02X %02X...",
             r,
             regs[r],
             regs[r + 1],
             regs[r + 2],
             regs[r + 3]);
  }
}
