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

#include "qmi8658a.h"

#include <string.h>

#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pin_def.h"

static const char *TAG = "QMI8658A";

#define QMI8658A_REG_WHO_AM_I 0x00
#define QMI8658A_REG_CTRL1    0x02
#define QMI8658A_REG_CTRL2    0x03
#define QMI8658A_REG_CTRL3    0x04
#define QMI8658A_REG_CTRL7    0x08
#define QMI8658A_REG_STATUS0  0x2E
#define QMI8658A_REG_AX_L     0x35
#define QMI8658A_REG_GX_L     0x3B
#define QMI8658A_REG_RESET    0x60
#define QMI8658A_RESET_MAGIC  0xB0

#define QMI8658A_CTRL1_VALUE 0x40

#define QMI8658A_CTRL2_VALUE     ((0x1 << 4) | 0x06)
#define QMI8658A_ACCEL_LSB_PER_G 8192.0f

#define QMI8658A_CTRL3_VALUE      ((0x5 << 4) | 0x06)
#define QMI8658A_GYRO_LSB_PER_DPS 64.0f

#define QMI8658A_CTRL7_VALUE 0x03

#define QMI8658A_SPI_FREQ_HZ (4 * 1000 * 1000)

#define QMI8658A_MAX_READ_LEN           16
#define QMI8658A_BUS_ACQUIRE_TIMEOUT_MS 5000

static spi_device_handle_t s_spi = NULL;

static esp_err_t read_regs(uint8_t reg, uint8_t *buf, size_t n) {
  if (s_spi == NULL || buf == NULL || n == 0 || n > QMI8658A_MAX_READ_LEN) {
    return ESP_ERR_INVALID_ARG;
  }
  uint8_t tx[1 + QMI8658A_MAX_READ_LEN] = {0};
  uint8_t rx[1 + QMI8658A_MAX_READ_LEN] = {0};
  tx[0] = reg | 0x80;
  spi_transaction_t t = {
      .length = 8 * (1 + n),
      .tx_buffer = tx,
      .rx_buffer = rx,
  };
  esp_err_t ret = spi_device_polling_transmit(s_spi, &t);
  if (ret != ESP_OK) {
    return ret;
  }
  memcpy(buf, &rx[1], n);
  return ESP_OK;
}

static esp_err_t write_reg(uint8_t reg, uint8_t val) {
  if (s_spi == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  uint8_t tx[2] = {reg & 0x7F, val};
  spi_transaction_t t = {
      .length = 16,
      .tx_buffer = tx,
  };
  return spi_device_polling_transmit(s_spi, &t);
}

esp_err_t qmi8658a_init(void) {
  if (s_spi != NULL) {
    return ESP_OK;
  }
  spi_device_interface_config_t devcfg = {
      .clock_speed_hz = QMI8658A_SPI_FREQ_HZ,
      .mode = 0,
      .spics_io_num = GPIO_QMI8658A_CS_PIN,
      .queue_size = 2,
  };
  esp_err_t ret = spi_bus_add_device(SPI3_HOST, &devcfg, &s_spi);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_add_device: %s", esp_err_to_name(ret));
    return ret;
  }
  return ESP_OK;
}

esp_err_t qmi8658a_read_chip_id(uint8_t *out_id) {
  if (out_id == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  return read_regs(QMI8658A_REG_WHO_AM_I, out_id, 1);
}

static esp_err_t write_verify(uint8_t reg, uint8_t val) {
  esp_err_t ret = write_reg(reg, val);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "write reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
    return ret;
  }
  vTaskDelay(pdMS_TO_TICKS(2));
  uint8_t got = 0xAA;
  ret = read_regs(reg, &got, 1);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "readback reg 0x%02X failed: %s", reg, esp_err_to_name(ret));
    return ret;
  }
  ESP_LOGI(TAG,
           "wrote 0x%02X -> reg 0x%02X, readback 0x%02X %s",
           val,
           reg,
           got,
           (got == val) ? "OK" : "MISMATCH");
  return ESP_OK;
}

esp_err_t qmi8658a_configure(void) {
  if (s_spi == NULL)
    return ESP_ERR_INVALID_STATE;
  bool acq =
      (spi_device_acquire_bus(s_spi, pdMS_TO_TICKS(QMI8658A_BUS_ACQUIRE_TIMEOUT_MS)) == ESP_OK);
  if (!acq) {
    ESP_LOGW(TAG, "configure: acquire_bus timeout — proceeding unlocked");
  }
  esp_err_t ret;
  ret = write_verify(QMI8658A_REG_CTRL1, QMI8658A_CTRL1_VALUE);
  if (ret == ESP_OK)
    ret = write_verify(QMI8658A_REG_CTRL2, QMI8658A_CTRL2_VALUE);
  if (ret == ESP_OK)
    ret = write_verify(QMI8658A_REG_CTRL3, QMI8658A_CTRL3_VALUE);
  if (ret == ESP_OK)
    ret = write_verify(QMI8658A_REG_CTRL7, QMI8658A_CTRL7_VALUE);
  if (acq) {
    spi_device_release_bus(s_spi);
  }
  if (ret != ESP_OK)
    return ret;
  vTaskDelay(pdMS_TO_TICKS(20));
  return ESP_OK;
}

static esp_err_t read_vec3(uint8_t reg, float lsb_per_unit, qmi8658a_vec3_t *out) {
  if (out == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  uint8_t buf[6];
  esp_err_t ret = read_regs(reg, buf, sizeof(buf));
  if (ret != ESP_OK) {
    return ret;
  }
  int16_t raw_x = (int16_t)((buf[1] << 8) | buf[0]);
  int16_t raw_y = (int16_t)((buf[3] << 8) | buf[2]);
  int16_t raw_z = (int16_t)((buf[5] << 8) | buf[4]);
  out->x = (float)raw_x / lsb_per_unit;
  out->y = (float)raw_y / lsb_per_unit;
  out->z = (float)raw_z / lsb_per_unit;
  return ESP_OK;
}

esp_err_t qmi8658a_read_accel(qmi8658a_vec3_t *out_g) {
  return read_vec3(QMI8658A_REG_AX_L, QMI8658A_ACCEL_LSB_PER_G, out_g);
}

esp_err_t qmi8658a_read_gyro(qmi8658a_vec3_t *out_dps) {
  return read_vec3(QMI8658A_REG_GX_L, QMI8658A_GYRO_LSB_PER_DPS, out_dps);
}

esp_err_t qmi8658a_data_ready(bool *out_accel, bool *out_gyro) {
  uint8_t status = 0;
  esp_err_t ret = read_regs(QMI8658A_REG_STATUS0, &status, 1);
  if (ret != ESP_OK) {
    return ret;
  }
  if (out_accel)
    *out_accel = (status & 0x01) != 0;
  if (out_gyro)
    *out_gyro = (status & 0x02) != 0;
  return ESP_OK;
}
