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


#ifndef IR_SAMSUNG_TV_H
#define IR_SAMSUNG_TV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <driver/gpio.h>
#include <esp_err.h>
#include <driver/rmt_tx.h>
#include <driver/rmt_rx.h>
#include <driver/rmt_encoder.h>

// Samsung TV IR Codes (exemplos comuns; podem variar por modelo)
#define TV_SAMSUNG_ON       0xE0E09966
#define TV_SAMSUNG_OFF      0xE0E019E6
#define TV_SAMSUNG_CH_UP    0xE0E048B7
#define TV_SAMSUNG_CH_DOWN  0xE0E008F7
#define TV_SAMSUNG_VOL_UP   0xE0E0E01F
#define TV_SAMSUNG_VOL_DOWN 0xE0E0D02F
#define TV_SAMSUNG_SOURCE   0xE0E0807F
#define TV_SAMSUNG_MUTE     0xE0E0F00F

// Protocolo Samsung 32 bits
#define SAMSUNG_BITS                32
#define SAMSUNG_HEADER_HIGH_US      4480
#define SAMSUNG_HEADER_LOW_US       4480
#define SAMSUNG_BIT_ONE_HIGH_US     560
#define SAMSUNG_BIT_ONE_LOW_US      1680
#define SAMSUNG_BIT_ZERO_HIGH_US    560
#define SAMSUNG_BIT_ZERO_LOW_US     560
#define SAMSUNG_END_HIGH_US         560

typedef struct {
    uint32_t address;  // 16 bits válidos
    uint32_t command;  // 16 bits válidos (cmd | ~cmd)
    uint32_t raw_data; // 32 bits crus
} samsung_ir_data_t;

// API
esp_err_t ir_samsung_init(gpio_num_t tx_gpio, gpio_num_t rx_gpio, bool invert_rx);
esp_err_t ir_samsung_send(uint32_t data);
esp_err_t ir_samsung_receive(samsung_ir_data_t* out, uint32_t timeout_ms);

// Contínuo: inicia, e cada chamada retorna o último evento; rearmado automaticamente
esp_err_t ir_samsung_start_continuous_receive(void);
esp_err_t ir_samsung_poll_last(samsung_ir_data_t* out, uint32_t timeout_ms);

void ir_samsung_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // IR_SAMSUNG_TV_H
