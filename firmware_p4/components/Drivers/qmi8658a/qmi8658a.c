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
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "esp_rom_sys.h"
#include "soc/gpio_sig_map.h"
#include "esp_log.h"
#include "esp_timer.h"
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
#define QMI_SPI_HZ           1000000
#define QMI_LOCK_MS          100

static spi_device_handle_t s_spi;
static bool s_ready;
static uint8_t s_revision;
static uint8_t s_odr = 6;
static int64_t s_settle_until;

#define QMI_REG_STATUSINT      0x2D
#define QMI_REG_GZ_H           0x40
#define QMI_SYNC_6DOF          0x83
#define QMI_AVAIL              0x01
#define QMI_LOCKED             0x02
#define QMI_LOCK_DELAY_US      20
#define QMI_SETTLE_US          5000000
#define QMI_MANUAL_CS_DELAY_US 2
#define QMI_SAMPLE_BYTES       14

/* Manual CS leaves the three hardware CS slots available to LCD and radios. */
static esp_err_t transfer(spi_transaction_t *transaction) {
  gpio_set_level(GPIO_QMI8658A_CS_PIN, 0);
  esp_rom_delay_us(QMI_MANUAL_CS_DELAY_US);
  esp_err_t err = spi_device_transmit(s_spi, transaction);
  esp_rom_delay_us(QMI_MANUAL_CS_DELAY_US);
  gpio_set_level(GPIO_QMI8658A_CS_PIN, 1);
  return err;
}

/* The caller owns the shared SPI lock for the complete operation. */
static esp_err_t reg_read(uint8_t reg, uint8_t *out) {
  spi_transaction_t t = {.length = 16, .flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_USE_RXDATA};
  t.tx_data[0] = reg | QMI_SPI_READ;
  esp_err_t err = transfer(&t);
  if (err == ESP_OK)
    *out = t.rx_data[1];
  return err;
}

static esp_err_t reg_write_checked(uint8_t reg, uint8_t value) {
  uint8_t who = 0, actual = 0;
  esp_err_t err = reg_read(QMI_REG_WHO_AM_I, &who);
  if (err != ESP_OK)
    return err;
  if (who != QMI_WHO_AM_I)
    return ESP_ERR_INVALID_RESPONSE;
  spi_transaction_t t = {.length = 16, .flags = SPI_TRANS_USE_TXDATA};
  t.tx_data[0] = reg;
  t.tx_data[1] = value;
  err = transfer(&t);
  vTaskDelay(pdMS_TO_TICKS(2));
  if (err == ESP_OK)
    err = reg_read(QMI_REG_WHO_AM_I, &who);
  if (err == ESP_OK && who != QMI_WHO_AM_I)
    err = ESP_ERR_INVALID_RESPONSE;
  if (err == ESP_OK)
    err = reg_read(reg, &actual);
  if (err == ESP_OK && actual != value)
    err = ESP_ERR_INVALID_RESPONSE;
  return err;
}

static int16_t le_i16(const uint8_t *p) {
  return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static esp_err_t configure_locked(uint8_t odr) {
  esp_err_t err = reg_write_checked(QMI_REG_CTRL7, 0);
  uint8_t ignored = 0;
  if (err == ESP_OK)
    err = reg_read(QMI_REG_GZ_H, &ignored);
  if (err == ESP_OK)
    err = reg_write_checked(QMI_REG_CTRL2, QMI_ACCEL_FS_8G | odr);
  if (err == ESP_OK)
    err = reg_write_checked(QMI_REG_CTRL3, QMI_GYRO_FS_512DPS | odr);
  if (err == ESP_OK)
    err = reg_write_checked(QMI_REG_CTRL5, QMI_FILTERS_ENABLE);
  if (err == ESP_OK)
    err = reg_write_checked(QMI_REG_CTRL7, QMI_SYNC_6DOF);
  if (err == ESP_OK) {
    s_odr = odr;
    s_settle_until = esp_timer_get_time() + QMI_SETTLE_US;
    s_ready = true;
  } else {
    s_ready = false;
    /* The checked helper refuses cleanup writes if identity has been lost. */
    (void)reg_write_checked(QMI_REG_CTRL7, 0);
  }
  return err;
}

esp_err_t qmi8658a_init(void) {
  if (!spi_bus_lock_take(QMI_LOCK_MS))
    return ESP_ERR_TIMEOUT;
  esp_err_t err = ESP_OK;
  if (s_ready)
    goto cleanup;
  if (s_spi == NULL) {
    esp_rom_gpio_connect_out_signal(GPIO_QMI8658A_CS_PIN, SIG_GPIO_OUT_IDX, false, false);
    gpio_set_level(GPIO_QMI8658A_CS_PIN, 1);
    err = gpio_set_direction(GPIO_QMI8658A_CS_PIN, GPIO_MODE_OUTPUT);
    if (err != ESP_OK)
      goto cleanup;
    spi_device_config_t cfg = {
        .cs_pin = -1, .clock_speed_hz = QMI_SPI_HZ, .mode = 0, .queue_size = 1};
    err = spi_add_device(SPI3_HOST, SPI_DEVICE_IMU, &cfg);
    if (err != ESP_OK)
      goto cleanup;
    s_spi = spi_get_handle(SPI_DEVICE_IMU);
    if (s_spi == NULL) {
      err = ESP_ERR_INVALID_STATE;
      goto cleanup;
    }
  }
  uint8_t who = 0, interface = 0;
  err = reg_read(QMI_REG_WHO_AM_I, &who);
  if (err == ESP_OK && who != QMI_WHO_AM_I)
    err = ESP_ERR_NOT_FOUND;
  if (err == ESP_OK)
    err = reg_read(QMI_REG_REVISION, &s_revision);
  if (err == ESP_OK)
    err = reg_read(QMI_REG_CTRL1, &interface);
  /* This unit was qualified with the cold-start interface value. Never rewrite it. */
  if (err == ESP_OK && interface != 0x20)
    err = ESP_ERR_INVALID_RESPONSE;
  if (err == ESP_OK)
    err = configure_locked(s_odr);
  if (err == ESP_OK) {
    ESP_LOGI(TAG,
             "IMU ready REV=%02x CTRL1=%02x preserved; synchronized samples",
             s_revision,
             interface);
  } else if (s_spi != NULL) {
    esp_err_t removed = spi_remove_device(SPI_DEVICE_IMU);
    if (removed == ESP_OK)
      s_spi = NULL;
  }
cleanup:
  spi_bus_lock_give();
  return err;
}

esp_err_t qmi8658a_start(void) {
  return qmi8658a_init();
}

esp_err_t qmi8658a_stop(void) {
  if (!spi_bus_lock_take(QMI_LOCK_MS))
    return ESP_ERR_TIMEOUT;
  esp_err_t err = ESP_OK;
  if (s_spi != NULL) {
    err = reg_write_checked(QMI_REG_CTRL7, 0);
    uint8_t ignored = 0;
    if (err == ESP_OK)
      err = reg_read(QMI_REG_GZ_H, &ignored);
    if (err == ESP_OK) {
      s_ready = false;
      err = spi_remove_device(SPI_DEVICE_IMU);
      if (err == ESP_OK)
        s_spi = NULL;
    }
  }
  spi_bus_lock_give();
  return err;
}

esp_err_t qmi8658a_set_odr(uint8_t odr_bits) {
  if (odr_bits < 5 || odr_bits > 8)
    return ESP_ERR_INVALID_ARG;
  esp_err_t err = qmi8658a_init();
  if (err != ESP_OK)
    return err;
  if (!spi_bus_lock_take(QMI_LOCK_MS))
    return ESP_ERR_TIMEOUT;
  if (!s_ready)
    err = ESP_ERR_INVALID_STATE;
  else if (odr_bits != s_odr)
    err = configure_locked(odr_bits);
  spi_bus_lock_give();
  return err;
}

esp_err_t qmi8658a_read(qmi8658a_sample_t *out) {
  if (out == NULL)
    return ESP_ERR_INVALID_ARG;
  if (!spi_bus_lock_take(QMI_LOCK_MS))
    return ESP_ERR_TIMEOUT;
  esp_err_t err = ESP_OK;
  uint8_t status = 0, raw[QMI_SAMPLE_BYTES] = {0};
  bool needs_unlock = false;
  if (!s_ready) {
    err = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }
  if (esp_timer_get_time() < s_settle_until) {
    err = ESP_ERR_NOT_FINISHED;
    goto cleanup;
  }
  /* STATUSINT can lock a sample even when a transport error hides its response. */
  needs_unlock = true;
  err = reg_read(QMI_REG_STATUSINT, &status);
  if (err != ESP_OK)
    goto cleanup;
  if (!(status & QMI_AVAIL)) {
    needs_unlock = false;
    err = ESP_ERR_NOT_FINISHED;
    goto cleanup;
  }
  if (!(status & QMI_LOCKED)) {
    esp_rom_delay_us(QMI_LOCK_DELAY_US);
    err = reg_read(QMI_REG_STATUSINT, &status);
    if (err == ESP_OK && !(status & QMI_LOCKED))
      err = ESP_ERR_TIMEOUT;
  }
  for (uint8_t i = 0; i < QMI_SAMPLE_BYTES && err == ESP_OK; ++i)
    err = reg_read(QMI_REG_TEMP_L + i, &raw[i]);
  if (err != ESP_OK)
    goto cleanup;
  needs_unlock = false; /* Reading GZ_H released the chip's sample lock. */
  qmi8658a_sample_t sample = {.status = status};
  sample.temperature_c = (float)le_i16(raw) / 256.0f;
  for (uint8_t i = 0; i < 3; ++i) {
    sample.accel_g[i] = (float)le_i16(raw + 2 + i * 2) / 4096.0f;
    sample.gyro_dps[i] = (float)le_i16(raw + 8 + i * 2) / 64.0f;
  }
  *out = sample;
cleanup:
  if (needs_unlock) {
    uint8_t ignored = 0;
    esp_err_t unlocked = reg_read(QMI_REG_GZ_H, &ignored);
    if (unlocked != ESP_OK)
      s_ready = false;
  }
  spi_bus_lock_give();
  return err;
}

bool qmi8658a_is_ready(void) {
  return s_ready;
}
uint8_t qmi8658a_revision(void) {
  return s_revision;
}
