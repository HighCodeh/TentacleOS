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

#ifndef ST25R3916_IRQ_H
#define ST25R3916_IRQ_H

#include <stdint.h>
#include <stdbool.h>
#include "highboy_nfc_error.h"

typedef struct {
    uint8_t main;       /* REG_MAIN_INT (0x1A) */
    uint8_t timer;      /* REG_TIMER_NFC_INT (0x1B) */
    uint8_t error;      /* REG_ERROR_INT (0x1C) */
    uint8_t target;     /* REG_TARGET_INT (0x1D) */
    uint8_t collision;  /* REG_COLLISION (0x20) */
} st25r_irq_status_t;

st25r_irq_status_t st25r_irq_read(void);

void st25r_irq_log(const char* ctx, uint16_t fifo_count);
bool st25r_irq_wait_txe(void);

#endif
