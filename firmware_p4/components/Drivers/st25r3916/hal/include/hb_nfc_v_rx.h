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

#ifndef HB_NFC_V_RX_H
#define HB_NFC_V_RX_H

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "hb_nfc_v_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t hb_nfc_v_rx_init(gpio_num_t pin);
esp_err_t hb_nfc_v_rx_deinit(void);
/* The owning task keeps the ST25R3916 in transparent mode throughout capture.
 * Timeout and malformed/overflowed captures never expose a partial frame. */
esp_err_t hb_nfc_v_rx_capture(
    uint8_t *buffer, size_t capacity, size_t *out_length, bool *out_slot, int64_t *out_eof_us);

#ifdef __cplusplus
}
#endif
#endif
