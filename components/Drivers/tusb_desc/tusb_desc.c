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


#include "tusb_desc.h" // Inclui o nosso próprio header
#include "tinyusb.h"
#include "esp_log.h"   // Adicionado para usar ESP_LOGI
#include <string.h>    // Adicionado para usar memcpy e strlen

static const char* TAG = "TUSB_DESC";

// Se você usa um Report ID no seu bad_usb.c, ele DEVE ser definido aqui também.
#define REPORT_ID_KEYBOARD 1

//--------------------------------------------------------------------+
// Descritor de Dispositivo (Device Descriptor)
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200, // USB 2.0
    .bDeviceClass       = 0x00,   // Classe definida na Interface
    .bDeviceSubClass    = 0x00,   // Subclasse definida na Interface
    .bDeviceProtocol    = 0x00,   // Protocolo definido na Interface
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor           = 0xCAFE, // ATENÇÃO: Use um Vendor ID seu. Este é para exemplo.
    .idProduct          = 0x4001, // ATENÇÃO: Use um Product ID seu.
    .bcdDevice          = 0x0100, // Versão 1.0.0 do dispositivo

    .iManufacturer      = 0x01,   // Índice para o descritor de string do Fabricante
    .iProduct           = 0x02,   // Índice para o descritor de string do Produto
    .iSerialNumber      = 0x03,   // Índice para o descritor de string do N° de Série

    .bNumConfigurations = 0x01    // Este dispositivo tem apenas 1 configuração
};

//--------------------------------------------------------------------+
// Descritor HID (HID Report Descriptor)
//--------------------------------------------------------------------+
uint8_t const desc_hid_report[] = {
    // A macro deve incluir o Report ID para ser consistente com o seu bad_usb.c
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD))
};

//--------------------------------------------------------------------+
// Descritor de Configuração (Configuration Descriptor)
//--------------------------------------------------------------------+
#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

uint8_t const desc_configuration[] = {
    // Cabeçalho da Configuração: 1 interface, etc.
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Descritor da Interface HID (Teclado)
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(desc_hid_report), 0x81, CFG_TUD_HID_EP_BUFSIZE, 1)
};

//--------------------------------------------------------------------+
// Descritores de String (String Descriptors)
//--------------------------------------------------------------------+
char const* string_desc_arr[] = {
    (char[]){0x09, 0x04}, // 0: Suporte de Idioma (Inglês)
    "HighCode",          // 1: Fabricante
    "BadUSB Device",     // 2: Produto
    "123456",            // 3: Número de Série
};

static uint16_t _desc_str[32];

//--------------------------------------------------------------------+
// Callbacks do TinyUSB
//--------------------------------------------------------------------+

// Retorna o Descritor de Dispositivo
const uint8_t* tud_descriptor_device_cb(void) {
    return (const uint8_t*) &desc_device;
}

// Retorna o Descritor de Configuração
const uint8_t* tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return desc_configuration;
}

// Retorna um Descritor de String
const uint16_t* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
    uint8_t chr_count;

    if (index == 0) {
        memcpy(&_desc_str[1], string_desc_arr[0], 2);
        chr_count = 1;
    } else {
        if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) return NULL;
        const char* str = string_desc_arr[index];
        chr_count = strlen(str);
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }
    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
    return _desc_str;
}


// --- Callbacks Específicos do HID (Movidos do bad_usb.c para cá) ---

// Retorna o Descritor HID Report
const uint8_t* tud_hid_descriptor_report_cb(uint8_t instance) {
    (void) instance;
    return desc_hid_report;
}

// Invocado quando o Host pede um report (ex: estado do teclado)
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    // Esta função não é usada para um teclado simples, mas precisa existir.
    (void) instance; (void) report_id; (void) report_type; (void) buffer; (void) reqlen;
    return 0;
}

// Invocado quando o Host envia um report (ex: para ligar o LED do Caps Lock)
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    // Esta função não é usada para este exemplo, mas precisa existir.
    (void) instance;
}


//--------------------------------------------------------------------+
// Função de Inicialização
//--------------------------------------------------------------------+
void busb_init(void){
    ESP_LOGI(TAG, "Inicializando o driver TinyUSB para BadUSB...");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = &desc_device,
        .string_descriptor = string_desc_arr,
        .string_descriptor_count = sizeof(string_desc_arr) / sizeof(string_desc_arr[0]),
        .external_phy = false,
        .configuration_descriptor = desc_configuration,
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "Driver TinyUSB instalado com sucesso.");
}
