// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GATT_EXPLORER_H
#define GATT_EXPLORER_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "spi_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

bool gatt_explorer_start(const uint8_t *addr, uint8_t addr_type);
bool gatt_explorer_is_busy(void);
esp_err_t gatt_explorer_get_status(spi_gatt_status_t *out);
esp_err_t gatt_explorer_get_record(uint8_t index, spi_gatt_record_t *out);
esp_err_t gatt_explorer_read(uint16_t handle);
esp_err_t gatt_explorer_stop(void);

#ifdef __cplusplus
}
#endif
#endif // GATT_EXPLORER_H
