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



#include "freertos/FreeRTOS.h"
#include <stdbool.h>
#include "freertos/task.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "tusb_desc.h"
#include "bad_usb.h"
#include "hid_hal.h"

static const char *TAG = "BAD_USB_MODULE";
#define REPORT_ID_KEYBOARD 1
#define REPORT_ID_MOUSE    2

static void usb_hid_send_report(uint8_t keycode, uint8_t modifier) {
  uint8_t keycode_array[6] = {0};
  keycode_array[0] = keycode;
  tud_hid_keyboard_report(REPORT_ID_KEYBOARD, modifier, keycode_array);
}

static void usb_hid_send_mouse(int8_t x, int8_t y, uint8_t buttons, int8_t wheel) {
  tud_hid_mouse_report(REPORT_ID_MOUSE, buttons, x, y, wheel, 0);
}

void bad_usb_wait_for_connection(void) {
  ESP_LOGI(TAG, "Aguardando conexao USB para executar o payload...");
  while (!tud_mounted()) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  ESP_LOGI(TAG, "Dispositivo conectado. Executando em 2 segundos...");
  vTaskDelay(pdMS_TO_TICKS(2000));
}

static bool s_is_initialized = false;

void bad_usb_init(void) {
  if (s_is_initialized) {
    ESP_LOGW(TAG, "BadUSB ja esta inicializado. Ignorando nova inicializacao.");
    return;
  }
  busb_init();
  hid_hal_register_callback(usb_hid_send_report, usb_hid_send_mouse, bad_usb_wait_for_connection);
  s_is_initialized = true;
}

void bad_usb_deinit(void) {
  if (!s_is_initialized) {
    ESP_LOGW(TAG, "BadUSB nao esta inicializado. Ignorando desinstalacao.");
    return;
  }
  ESP_LOGI(TAG, "Finalizando o modo BadUSB e desinstalando o driver...");
  hid_hal_register_callback(NULL, NULL, NULL);

  esp_err_t err = tinyusb_driver_uninstall();
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Driver TinyUSB desinstalado com sucesso.");
  } else {
    ESP_LOGE(TAG, "Falha ao desinstalar Driver TinyUSB: %d", err);
  }
  s_is_initialized = false;
}
