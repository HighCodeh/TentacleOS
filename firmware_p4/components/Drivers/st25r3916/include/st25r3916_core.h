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

#ifndef ST25R3916_CORE_H
#define ST25R3916_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#include "highboy_nfc.h"

/**
 * @brief Initialize the ST25R3916 core.
 */
esp_err_t st25r3916_core_init(const highboy_nfc_config_t *config);

/**
 * @brief Deinitialize after disabling the field; retain the driver on failure.
 * @return ESP_OK on success, or the field-off transport error.
 */
esp_err_t st25r3916_core_deinit(void);

/**
 * @brief Read and validate the chip IC identity.
 */
esp_err_t st25r3916_core_check_id(uint8_t *out_id, uint8_t *out_type, uint8_t *out_rev);

/**
 * @brief Enable the RF field.
 */
esp_err_t st25r3916_core_field_on(void);

/**
 * @brief Disable the RF field.
 */
esp_err_t st25r3916_core_field_off(void);

/**
 * @brief Check if the RF field is active.
 */
bool st25r3916_core_field_is_on(void);

/**
 * @brief Power-cycle the RF field.
 */
esp_err_t st25r3916_core_field_cycle(void);

/**
 * @brief Configure the chip for ISO 14443-A.
 */
esp_err_t st25r3916_core_set_mode_nfca(void);

/**
 * @brief Dump all registers for diagnostics.
 */
void st25r3916_core_dump_regs(void);

#ifdef __cplusplus
}
#endif

#endif // ST25R3916_CORE_H
