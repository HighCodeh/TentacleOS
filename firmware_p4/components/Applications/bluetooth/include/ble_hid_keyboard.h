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

#ifndef BLE_HID_KEYBOARD_H
#define BLE_HID_KEYBOARD_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Inicializa o serviço HID over GATT (HOGP) e começa a anunciar como Teclado.
 */
esp_err_t ble_hid_init(void);

/**
 * @brief Para o serviço HID e desconecta.
 */
esp_err_t ble_hid_deinit(void);

/**
 * @brief Verifica se há um host conectado e pronto para receber teclas.
 */
bool ble_hid_is_connected(void);

/**
 * @brief Envia uma tecla pressionada via Bluetooth.
 * 
 * @param keycode Código HID da tecla (ex: 0x04 para 'A')
 * @param modifier Modificador (Shift, Ctrl, Alt, GUI)
 */
void ble_hid_send_key(uint8_t keycode, uint8_t modifier);

#endif // BLE_HID_KEYBOARD_H
