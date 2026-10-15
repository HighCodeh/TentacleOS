// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "qmi8658a.h"

#include <string.h>

#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "pin_def.h"
#include "spi.h"

static const char *TAG = "QMI8658A";

#define QMI_REG_WHO_AM_I 0x00
#define QMI_REG_REVISION 0x01
#define QMI_REG_CTRL1    0x02
#define QMI_REG_CTRL2    0x03
#define QMI_REG_CTRL3    0x04
#define QMI_REG_CTRL5    0x06
#define QMI_REG_CTRL7    0x08
#define QMI_REG_STATUS0  0x2E
#define QMI_REG_TEMP_L   0x33
#define QMI_REG_RESET    0x60

#define QMI_WHO_AM_I         0x05
#define QMI_SPI_READ         0x80
#define QMI_CTRL1_ADDR_AI_LE 0x40
#define QMI_ACCEL_FS_8G      0x20
#define QMI_GYRO_FS_512DPS   0x50
#define QMI_FILTERS_ENABLE   0x11
#define QMI_ENABLE_6DOF      0x03
#define QMI_SPI_HZ           5000000
#define QMI_LOCK_MS          100

static spi_device_handle_t s_spi;
static bool s_ready;
static uint8_t s_revision;
static uint8_t s_odr = 6;

static esp_err_t transfer(const uint8_t *tx, uint8_t *rx, size_t len) {
  if (s_spi == NULL || tx == NULL || len == 0)
    return ESP_ERR_INVALID_STATE;
  if (!spi_bus_lock_take(QMI_LOCK_MS))
    return ESP_ERR_TIMEOUT;
  spi_transaction_t t = {
      .length = len * 8,
      .tx_buffer = tx,
      .rx_buffer = rx,
  };
  esp_err_t err = spi_device_transmit(s_spi, &t);
  spi_bus_lock_give();
  return err;
}

static esp_err_t reg_write(uint8_t reg, uint8_t value) {
  uint8_t tx[2] = {(uint8_t)(reg & 0x7F), value};
  return transfer(tx, NULL, sizeof(tx));
}

static esp_err_t reg_read(uint8_t reg, uint8_t *data, size_t len) {
  if (data == NULL || len == 0 || len > 15)
    return ESP_ERR_INVALID_ARG;
  uint8_t tx[16] = {0};
  uint8_t rx[16] = {0};
  tx[0] = (uint8_t)(reg | QMI_SPI_READ);
  esp_err_t err = transfer(tx, rx, len + 1);
  if (err == ESP_OK)
    memcpy(data, &rx[1], len);
  return err;
}

static int16_t le_i16(const uint8_t *p) {
  return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

esp_err_t qmi8658a_init(void) {
  if (s_ready)
    return ESP_OK;

  spi_device_config_t cfg = {
      .cs_pin = GPIO_QMI8658A_CS_PIN,
      .clock_speed_hz = QMI_SPI_HZ,
      .mode = 0,
      .queue_size = 1,
  };
  esp_err_t err = spi_add_device(SPI3_HOST, SPI_DEVICE_IMU, &cfg);
  if (err == ESP_ERR_INVALID_STATE) {
    ESP_RETURN_ON_ERROR(spi_init(), TAG, "shared SPI init failed");
    err = spi_add_device(SPI3_HOST, SPI_DEVICE_IMU, &cfg);
  }
  ESP_RETURN_ON_ERROR(err, TAG, "SPI device add failed");
  s_spi = spi_get_handle(SPI_DEVICE_IMU);
  if (s_spi == NULL)
    return ESP_ERR_INVALID_STATE;

  uint8_t who = 0;
  ESP_RETURN_ON_ERROR(reg_read(QMI_REG_WHO_AM_I, &who, 1), TAG, "WHO_AM_I read failed");
  if (who != QMI_WHO_AM_I) {
    ESP_LOGE(TAG, "WHO_AM_I mismatch: 0x%02X", who);
    return ESP_ERR_NOT_FOUND;
  }

  ESP_RETURN_ON_ERROR(reg_write(QMI_REG_RESET, 0xB0), TAG, "reset failed");
  vTaskDelay(pdMS_TO_TICKS(20));
  ESP_RETURN_ON_ERROR(reg_read(QMI_REG_WHO_AM_I, &who, 1), TAG, "WHO_AM_I after reset failed");
  if (who != QMI_WHO_AM_I)
    return ESP_ERR_INVALID_RESPONSE;

  (void)reg_read(QMI_REG_REVISION, &s_revision, 1);
  ESP_RETURN_ON_ERROR(reg_write(QMI_REG_CTRL1, QMI_CTRL1_ADDR_AI_LE), TAG, "CTRL1 failed");
  ESP_RETURN_ON_ERROR(reg_write(QMI_REG_CTRL2, QMI_ACCEL_FS_8G | s_odr), TAG, "CTRL2 failed");
  ESP_RETURN_ON_ERROR(reg_write(QMI_REG_CTRL3, QMI_GYRO_FS_512DPS | s_odr), TAG, "CTRL3 failed");
  ESP_RETURN_ON_ERROR(reg_write(QMI_REG_CTRL5, QMI_FILTERS_ENABLE), TAG, "CTRL5 failed");
  ESP_RETURN_ON_ERROR(reg_write(QMI_REG_CTRL7, QMI_ENABLE_6DOF), TAG, "CTRL7 failed");
  vTaskDelay(pdMS_TO_TICKS(20));
  s_ready = true;
  ESP_LOGI(TAG, "QMI8658A ready, revision 0x%02X", s_revision);
  return ESP_OK;
}

esp_err_t qmi8658a_start(void) {
  ESP_RETURN_ON_ERROR(qmi8658a_init(), TAG, "init failed");
  return reg_write(QMI_REG_CTRL7, QMI_ENABLE_6DOF);
}

esp_err_t qmi8658a_stop(void) {
  if (!s_ready)
    return ESP_OK;
  return reg_write(QMI_REG_CTRL7, 0x00);
}

esp_err_t qmi8658a_set_odr(uint8_t odr_bits) {
  if (odr_bits < 5 || odr_bits > 8)
    return ESP_ERR_INVALID_ARG;
  ESP_RETURN_ON_ERROR(qmi8658a_init(), TAG, "init failed");
  ESP_RETURN_ON_ERROR(reg_write(QMI_REG_CTRL7, 0x00), TAG, "sensor pause failed");
  ESP_RETURN_ON_ERROR(reg_write(QMI_REG_CTRL2, QMI_ACCEL_FS_8G | odr_bits), TAG, "CTRL2 failed");
  ESP_RETURN_ON_ERROR(reg_write(QMI_REG_CTRL3, QMI_GYRO_FS_512DPS | odr_bits), TAG, "CTRL3 failed");
  s_odr = odr_bits;
  return reg_write(QMI_REG_CTRL7, QMI_ENABLE_6DOF);
}

esp_err_t qmi8658a_read(qmi8658a_sample_t *out) {
  if (out == NULL)
    return ESP_ERR_INVALID_ARG;
  ESP_RETURN_ON_ERROR(qmi8658a_init(), TAG, "init failed");

  uint8_t status = 0;
  (void)reg_read(QMI_REG_STATUS0, &status, 1);
  uint8_t raw[14] = {0};
  ESP_RETURN_ON_ERROR(reg_read(QMI_REG_TEMP_L, raw, sizeof(raw)), TAG, "sample read failed");

  memset(out, 0, sizeof(*out));
  out->status = status;
  out->temperature_c = (float)le_i16(&raw[0]) / 256.0f;
  for (int i = 0; i < 3; i++)
    out->accel_g[i] = (float)le_i16(&raw[2 + i * 2]) / 4096.0f;
  for (int i = 0; i < 3; i++)
    out->gyro_dps[i] = (float)le_i16(&raw[8 + i * 2]) / 64.0f;
  return ESP_OK;
}

bool qmi8658a_is_ready(void) {
  return s_ready;
}
uint8_t qmi8658a_revision(void) {
  return s_revision;
}
