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

#ifndef HIGHBOY_NFC_H
#define HIGHBOY_NFC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "pin_def.h"

/**
 * @brief Hardware and bus configuration for library initialization.
 *
 * Target: ST25R3916 / ST25R3916B.
 */
typedef struct {
  gpio_num_t pin_mosi;   /**< SPI MOSI pin. */
  gpio_num_t pin_miso;   /**< SPI MISO pin. */
  gpio_num_t pin_sclk;   /**< SPI clock pin. */
  gpio_num_t pin_cs;     /**< SPI chip-select pin. */
  gpio_num_t pin_irq;    /**< ST25R3916 IRQ output pin. */
  int spi_host;          /**< ESP-IDF SPI host index (e.g., SPI2_HOST). */
  uint8_t spi_mode;      /**< SPI mode (ST25R3916 requires Mode 1). */
  uint32_t spi_clock_hz; /**< SPI clock frequency (Max 6MHz). */
} highboy_nfc_config_t;

/**
 * @brief Default configuration for the HighBoy V2 board: the ST25R3916 sits on the
 * shared SPI3 bus (display / CC1101 / SX1262), with its own CS/IRQ from pin_def.h.
 * The SPI pins are the shared-bus pins; hb_nfc_spi_init() joins that bus rather
 * than initializing its own.
 */
#define HIGHBOY_NFC_CONFIG_DEFAULT()    \
  {                                     \
      .pin_mosi = GPIO_SPI_MOSI_PIN,    \
      .pin_miso = GPIO_SPI_MISO_PIN,    \
      .pin_sclk = GPIO_SPI_SCLK_PIN,    \
      .pin_cs = GPIO_NFC_CS_PIN,        \
      .pin_irq = GPIO_NFC_IRQ_PIN,      \
      .spi_host = SPI3_HOST,            \
      .spi_mode = 1,                    \
      .spi_clock_hz = 2000000,          \
  }

/**
 * @brief Initialize the NFC library and underlying hardware.
 *
 * @param[in] config Hardware configuration. Must not be NULL.
 * @return
 * - ESP_OK: Success.
 * - ESP_ERR_INVALID_ARG: NULL configuration.
 * - ESP_ERR_NOT_FOUND: Chip not detected.
 * - ESP_FAIL: Hardware initialization failed.
 */
esp_err_t highboy_nfc_init(const highboy_nfc_config_t *config);

/**
 * @brief Deinitialize the library and power down the RF field.
 */
void highboy_nfc_deinit(void);

/**
 * @brief Ping the chip to verify communication.
 *
 * @param[out] out_chip_id Optional pointer to store the raw IC identity.
 * @return ESP_OK on success.
 */
esp_err_t highboy_nfc_ping(uint8_t *out_chip_id);

/**
 * @brief Enable the RF field.
 * @return ESP_OK on success.
 */
esp_err_t highboy_nfc_field_on(void);

/**
 * @brief Disable the RF field.
 * @return ESP_OK on success.
 */
esp_err_t highboy_nfc_field_off(void);

/**
 * @brief Measure the current RF field amplitude.
 *
 * @return Raw ADC value (0–255).
 */
uint8_t highboy_nfc_measure_amplitude(void);

/**
 * @brief Detect if an external RF field is present (External Field Detector).
 *
 * @param[out] out_aux_display Optional pointer to store raw AUX_DISPLAY register.
 * @return true if field detected, false otherwise.
 */
bool highboy_nfc_field_detected(uint8_t *out_aux_display);

#ifdef __cplusplus
}
#endif

#endif // HIGHBOY_NFC_H
