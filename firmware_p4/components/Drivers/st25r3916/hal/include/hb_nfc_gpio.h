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
 * @file hb_nfc_gpio.h
 * @brief HAL GPIO IRQ pin monitoring.
 */
#ifndef HB_NFC_GPIO_H
#define HB_NFC_GPIO_H

#include <stdint.h>
#include <stdbool.h>
#include "highboy_nfc_error.h"

hb_nfc_err_t hb_gpio_init(int pin_irq);
void         hb_gpio_deinit(void);

/** Read IRQ pin level directly (0 or 1). */
int  hb_gpio_irq_level(void);

/** Wait for IRQ pin high with timeout (ms). Returns true if IRQ seen. */
bool hb_gpio_irq_wait(uint32_t timeout_ms);

#endif
