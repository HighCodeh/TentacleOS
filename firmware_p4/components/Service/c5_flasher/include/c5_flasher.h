#ifndef C5_FLASHER_H
#define C5_FLASHER_H

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief Initializes the pins and UART required to flash the C5
 */
esp_err_t c5_flasher_init(void);

/**
 * @brief Puts C5 into Bootloader mode
 */
void c5_flasher_enter_bootloader(void);

/**
 * @brief Resets C5 into normal operation mode
 */
void c5_flasher_reset_normal(void);

/**
 * @brief Performs the actual firmware update using the embedded binary
 * 
 * @param bin_data Pointer to the embedded C5 binary
 * @param bin_size Size of the binary
 * @return esp_err_t 
 */
esp_err_t c5_flasher_update(const uint8_t *bin_data, uint32_t bin_size);

#endif
