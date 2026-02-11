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


#ifndef PN7150_H
#define PN7150_H

#include "driver/i2c.h"
#include "driver/gpio.h"

// Configurações de hardware
#define I2C_MASTER_NUM         I2C_NUM_0
#define I2C_MASTER_SDA_IO      GPIO_NUM_8
#define I2C_MASTER_SCL_IO      GPIO_NUM_9
#define I2C_MASTER_FREQ_HZ     400000
#define PN7150_I2C_ADDRESS     0x28  // Endereço I2C (A0-A1 = GND)
#define PN7150_PIN_VEN         GPIO_NUM_18  // pino de controle VEN (reset)
#define PN7150_PIN_IRQ         GPIO_NUM_17 // pino de interrupcao IRQ

// Funções do driver
void pn7150_i2c_init(void);
void pn7150_hw_init(void);
esp_err_t pn7150_send_cmd(const uint8_t *data, size_t len);
esp_err_t pn7150_read_rsp(uint8_t *buffer, size_t *length);
esp_err_t pn7150_core_reset(void);
esp_err_t pn7150_core_init(void);

#endif // PN7150_H
