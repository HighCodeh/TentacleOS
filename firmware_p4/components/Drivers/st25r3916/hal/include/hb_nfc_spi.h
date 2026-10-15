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

/**
 * @file hb_nfc_spi.h
 * @brief HAL SPI register, FIFO, command, and PT memory access for the ST25R3916.
 *
 * SPI framing (ST25R3916 datasheet DocID 031020 Rev 3, Sec. 6):
 *  Read:    TX [0x40 | addr] [0x00]   — data in RX[1]
 *  Write:   TX [addr & 0x3F] [data]
 *  FIFO LD: TX [0x80] [data...]
 *  FIFO RD: TX [0x9F] [0x00...]       — data in RX[1+]
 *  Command: TX [cmd_byte]             — single byte
 */
#ifndef HB_NFC_SPI_H
#define HB_NFC_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#include "driver/gpio.h"
#include "esp_err.h"

/**
 * @brief SPI bus and device configuration for hb_nfc_spi_init().
 */
typedef struct {
  int spi_host;        /**< ESP-IDF SPI host index (SPI2_HOST = 1, SPI3_HOST = 2). */
  gpio_num_t pin_mosi; /**< MOSI pin. */
  gpio_num_t pin_miso; /**< MISO pin. */
  gpio_num_t pin_sclk; /**< SCLK pin. */
  gpio_num_t pin_cs;   /**< Chip-select pin (active low). */
  uint8_t mode;        /**< SPI mode (0–3). ST25R3916 requires mode 1. */
  uint32_t clock_hz;   /**< SPI clock in Hz. ST25R3916B datasheet max: 10 MHz. */
} hb_nfc_spi_config_t;

/**
 * @brief Initialize the SPI bus and register the ST25R3916 device.
 *
 * @param config  SPI configuration. Must not be NULL.
 * @return
 *   - ESP_OK on success or if already initialized
 *   - ESP_ERR_INVALID_ARG if config is NULL
 *   - HB_NFC_ERR_SPI_INIT if bus or device initialization fails
 */
esp_err_t hb_nfc_spi_init(const hb_nfc_spi_config_t *config);

/**
 * @brief Remove only the NFC device; the shared High Boy SPI bus remains active.
 */
void hb_nfc_spi_deinit(void);

/**
 * @brief Read a single ST25R3916 register.
 *
 * @param addr            Register address (6-bit).
 * @param[out] out_value  Receives the register byte. Must not be NULL.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG or HB_NFC_ERR_SPI_XFER on failure.
 */
esp_err_t hb_nfc_spi_reg_read(uint8_t addr, uint8_t *out_value);

/**
 * @brief Write a single ST25R3916 register.
 *
 * @param addr   Register address (6-bit).
 * @param value  Byte to write.
 * @return ESP_OK on success, HB_NFC_ERR_SPI_XFER on failure.
 */
esp_err_t hb_nfc_spi_reg_write(uint8_t addr, uint8_t value);

/** Read a single register from ST25R3916 register space B. */
esp_err_t hb_nfc_spi_reg_read_b(uint8_t addr, uint8_t *out_value);

/** Write a single register in ST25R3916 register space B. */
esp_err_t hb_nfc_spi_reg_write_b(uint8_t addr, uint8_t value);

/**
 * @brief Read-modify-write a register using a bitmask.
 *
 * @param addr   Register address.
 * @param mask   Bits to modify.
 * @param value  New values for the masked bits.
 * @return ESP_OK on success, or a SPI error code on failure.
 */
esp_err_t hb_nfc_spi_reg_modify(uint8_t addr, uint8_t mask, uint8_t value);

/**
 * @brief Load data into the ST25R3916 TX FIFO.
 *
 * @param data  Source buffer. Must not be NULL.
 * @param len   Byte count. Must be 1–512.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG or HB_NFC_ERR_SPI_XFER on failure.
 */
esp_err_t hb_nfc_spi_fifo_load(const uint8_t *data, size_t len);

/**
 * @brief Read received bytes from the ST25R3916 RX FIFO.
 *
 * @param data  Destination buffer. Must not be NULL.
 * @param len   Byte count. Must be 1–512.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG or HB_NFC_ERR_SPI_XFER on failure.
 */
esp_err_t hb_nfc_spi_fifo_read(uint8_t *data, size_t len);

/**
 * @brief Send a direct command byte to the ST25R3916.
 *
 * @param cmd  Command byte (see ST25R3916_CMD_* defines in st25r3916_cmd.h).
 * @return ESP_OK on success, HB_NFC_ERR_SPI_XFER on failure.
 */
esp_err_t hb_nfc_spi_direct_cmd(uint8_t cmd);

/**
 * @brief Write to ST25R3916 passive target memory.
 *
 * TX protocol: [prefix] [data0] ... [dataN]
 *
 * @param prefix  Memory section select byte (ST25R3916_SPI_PT_MEM_A_WRITE / F_WRITE / TSN_WRITE).
 * @param data    Payload. Must not be NULL.
 * @param len     Payload length. Must be 1–19.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hb_nfc_spi_pt_mem_write(uint8_t prefix, const uint8_t *data, size_t len);

/**
 * @brief Read from ST25R3916 passive target memory.
 *
 * The chip inserts a command byte and a turnaround byte before the payload on MISO.
 *
 * @param[out] out_data  Destination buffer. Must not be NULL.
 * @param len            Byte count. Must be 1–19.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hb_nfc_spi_pt_mem_read(uint8_t *out_data, size_t len);

/**
 * @brief Perform a raw multi-byte SPI transfer.
 *
 * @param tx        TX buffer. Must not be NULL.
 * @param[out] rx   RX buffer. May be NULL if receive data is not needed.
 * @param len       Transfer length. Must be 1–64.
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG or HB_NFC_ERR_SPI_XFER on failure.
 */
esp_err_t hb_nfc_spi_raw_xfer(const uint8_t *tx, uint8_t *rx, size_t len);

#ifdef __cplusplus
}
#endif

#endif // HB_NFC_SPI_H
