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

#ifndef SX1262_HAL_H
#define SX1262_HAL_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HAL contract for SX1262 driver.
 *
 * All hardware access is delegated through these function pointers.
 * The driver core never includes platform headers.
 * To port to a new platform, implement all callbacks in a new port/ file.
 *
 * ctx is an opaque pointer — the driver never inspects its contents.
 */
typedef struct sx1262_hal {
  /**
   * @brief Full-duplex SPI transfer. CS must be managed separately.
   * @param ctx  Opaque context.
   * @param tx   TX buffer (may be NULL for read-only).
   * @param rx   RX buffer (may be NULL for write-only).
   * @param len  Number of bytes to transfer.
   * @return 0 on success, < 0 on failure.
   */
  int (*spi_transfer)(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len);

  /**
   * @brief Assert chip select (NSS LOW).
   * @param ctx  Opaque context.
   */
  void (*cs_low)(void *ctx);

  /**
   * @brief De-assert chip select (NSS HIGH).
   * @param ctx  Opaque context.
   */
  void (*cs_high)(void *ctx);

  /**
   * @brief Write NRESET pin level. 0 = reset active (LOW).
   * @param ctx    Opaque context.
   * @param level  Pin level (0 or 1).
   */
  void (*reset_write)(void *ctx, uint8_t level);

  /**
   * @brief Read BUSY pin. Returns 1 if busy, 0 if ready. DS Section 8.3.1.
   * @param ctx  Opaque context.
   * @return 1 if busy, 0 if ready.
   */
  uint8_t (*busy_read)(void *ctx);

  /**
   * @brief Blocking delay in milliseconds. Used only during reset and boot.
   * @param ctx  Opaque context.
   * @param ms   Delay in milliseconds.
   */
  void (*delay_ms)(void *ctx, uint32_t ms);

  /**
   * @brief Monotonic timestamp in milliseconds for timeout calculations.
   * @param ctx  Opaque context.
   * @return Current tick count in milliseconds.
   */
  uint32_t (*get_tick_ms)(void *ctx);

  /**
   * @brief Lock SPI bus mutex. Called before every transaction.
   * @param ctx  Opaque context.
   */
  void (*lock)(void *ctx);

  /**
   * @brief Unlock SPI bus mutex. Called after every transaction.
   * @param ctx  Opaque context.
   */
  void (*unlock)(void *ctx);

  /**
   * @brief Disable interrupts. For ISR/task boundary protection.
   * @param ctx  Opaque context.
   */
  void (*enter_critical)(void *ctx);

  /**
   * @brief Re-enable interrupts.
   * @param ctx  Opaque context.
   */
  void (*exit_critical)(void *ctx);

  /**
   * @brief Set antenna switch mode.
   * @param ctx   Opaque context.
   * @param mode  0 = OFF, 1 = RX, 2 = TX. Platform-specific pin control.
   */
  void (*set_antenna)(void *ctx, uint8_t mode);

  /**
   * @brief Opaque context pointer. Driver never inspects.
   */
  void *ctx;
} sx1262_hal_t;

/**
 * @brief Create and initialize the HAL for SX1262.
 *
 * Configures SPI bus, GPIOs, and mutex. Fills out_hal with
 * all callback pointers and context.
 *
 * @param out_hal  Pointer to HAL struct to populate. Must not be NULL.
 * @return ESP_OK on success.
 */
esp_err_t sx1262_hal_create(sx1262_hal_t *out_hal);

/**
 * @brief Tear down the HAL so the next sx1262_hal_create() rebuilds it.
 *
 * Removes the SX1262 SPI device, deletes the SPI mutex, and clears the
 * internal initialized flag. The SPI3 bus itself is left initialized
 * because it is shared with the ST7789 display.
 *
 * Safe to call when the HAL is not initialized — returns ESP_OK as a no-op.
 *
 * @return ESP_OK.
 */
esp_err_t sx1262_hal_destroy(void);

#ifdef __cplusplus
}
#endif

#endif // SX1262_HAL_H
