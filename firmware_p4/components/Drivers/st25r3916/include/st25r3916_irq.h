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
 * @file st25r3916_irq.h
 * @brief ST25R3916 IRQ read/clear/log interrupt status.
 */
#ifndef ST25R3916_IRQ_H
#define ST25R3916_IRQ_H

#include <stdint.h>
#include <stdbool.h>
#include "highboy_nfc_error.h"

/** IRQ status snapshot (all 5 registers for target mode support). */
typedef struct {
    uint8_t main;       
    uint8_t timer;      
    uint8_t error;      
    uint8_t target;     
    uint8_t collision;  
} st25r_irq_status_t;

/** Read all IRQ registers (reading clears the flags). */
st25r_irq_status_t st25r_irq_read(void);

/** Log IRQ status with context string. */
void st25r_irq_log(const char* ctx, uint16_t fifo_count);

/**
 * Wait for TX end with a bounded timeout.
 */
bool st25r_irq_wait_txe(void);

#endif
