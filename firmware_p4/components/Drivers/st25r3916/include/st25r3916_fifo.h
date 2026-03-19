// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
/**
 * @file st25r3916_fifo.h
 * @brief ST25R3916 FIFO load, read, count, TX byte setup.
 */
#ifndef ST25R3916_FIFO_H
#define ST25R3916_FIFO_H

#include <stdint.h>
#include <stddef.h>
#include "highboy_nfc_error.h"

/** Get number of bytes currently in FIFO (0-512). */
uint16_t st25r_fifo_count(void);

/** Clear FIFO via direct command. */
void st25r_fifo_clear(void);

/** Load data into FIFO for transmission. Max 512 bytes (full FIFO). */
hb_nfc_err_t st25r_fifo_load(const uint8_t* data, size_t len);

/** Read data from FIFO. Max 512 bytes (full FIFO). */
hb_nfc_err_t st25r_fifo_read(uint8_t* data, size_t len);

/**
 * Set TX byte and bit count registers.
 */
void st25r_set_tx_bytes(uint16_t nbytes, uint8_t nbtx_bits);

/**
 * Wait for FIFO to reach min_bytes, polling every 1ms.
 * Returns actual count. Sets *final_count if non-NULL.
 */
int st25r_fifo_wait(size_t min_bytes, int timeout_ms, uint16_t* final_count);

#endif
