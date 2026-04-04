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


#ifndef BQ25896_H
#define BQ25896_H

#include "driver/i2c.h"
#include <stdint.h>
#include <stdbool.h>

// Endereço I2C do BQ25896
#define BQ25896_I2C_ADDR 0x6B

// Enum para o Status de Carregamento (conforme datasheet)
typedef enum {
    CHARGE_STATUS_NOT_CHARGING = 0,
    CHARGE_STATUS_PRECHARGE = 1,
    CHARGE_STATUS_FAST_CHARGE = 2,
    CHARGE_STATUS_CHARGE_DONE = 3
} bq25896_charge_status_t;

// Enum para o Status do VBUS (conforme datasheet)
typedef enum {
    VBUS_STATUS_UNKNOWN = 0,
    VBUS_STATUS_USB_HOST = 1,
    VBUS_STATUS_ADAPTER_PORT = 2,
    VBUS_STATUS_OTG = 3
} bq25896_vbus_status_t;

// --- Declaração das Funções Públicas ---

// Inicializa o BQ25896
esp_err_t bq25896_init(void);

// Obtém o status de carregamento
bq25896_charge_status_t bq25896_get_charge_status(void);

// Obtém o status do VBUS (se o carregador está conectado)
bq25896_vbus_status_t bq25896_get_vbus_status(void);

// Obtém a tensão da bateria em mV
uint16_t bq25896_get_battery_voltage(void);

// Retorna 'true' se estiver carregando (pré-carga ou carga rápida)
bool bq25896_is_charging(void);

// Estima a porcentagem da bateria com base na tensão
int bq25896_get_battery_percentage(uint16_t voltage_mv);

#endif // BQ25896_H
