// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.
#include "nfc_listener.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "nfc_device.h"
#include "mf_classic.h"
#include "mf_classic_emu.h"

static const char *TAG = "NFC_LISTENER";

static TaskHandle_t s_emu_task = NULL;
static volatile bool s_is_emu_running = false;

static void nfc_listener_task(void *arg) {
  (void)arg;
  ESP_LOGI(TAG, "emulation loop started");
  while (s_is_emu_running) {
    mfc_emu_run_step();
    taskYIELD();
  }
  ESP_LOGI(TAG, "emulation loop stopped");
  s_emu_task = NULL;
  vTaskDelete(NULL);
}

static hb_nfc_err_t start_emu_task(void) {
  s_is_emu_running = true;
  BaseType_t rc = xTaskCreatePinnedToCore(
      nfc_listener_task, "nfc_emu", 4096, NULL, SYS_PRIO_SERVICE_HI, &s_emu_task, SYS_CORE_RADIO);
  return (rc == pdPASS) ? HB_NFC_OK : HB_NFC_ERR_INTERNAL;
}

hb_nfc_err_t nfc_listener_start(const hb_nfc_card_data_t *card) {
  static mfc_emu_card_data_t emu;
  memset(&emu, 0, sizeof(emu));
  hb_nfc_err_t err;

  if (card && card->protocol == HB_PROTO_MF_CLASSIC) {
    mf_classic_type_t type = mf_classic_get_type(card->iso14443a.sak);
    mfc_emu_card_data_init(&emu, &card->iso14443a, type);
    err = mfc_emu_init(&emu);
  } else {
    int idx = nfc_device_get_active();
    if (idx < 0)
      return HB_NFC_ERR_PARAM;
    err = nfc_device_load(idx, &emu);
    if (err != HB_NFC_OK)
      return err;
    err = mfc_emu_init(&emu);
  }

  if (err != HB_NFC_OK)
    return err;
  err = mfc_emu_configure_target();
  if (err != HB_NFC_OK)
    return err;
  err = mfc_emu_start();
  if (err != HB_NFC_OK)
    return err;
  return start_emu_task();
}

hb_nfc_err_t nfc_listener_start_emu(const mfc_emu_card_data_t *emu) {
  hb_nfc_err_t err = mfc_emu_init(emu);
  if (err != HB_NFC_OK)
    return err;
  err = mfc_emu_configure_target();
  if (err != HB_NFC_OK)
    return err;
  err = mfc_emu_start();
  if (err != HB_NFC_OK)
    return err;
  return start_emu_task();
}

void nfc_listener_stop(void) {
  s_is_emu_running = false;
  while (s_emu_task != NULL) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  mfc_emu_stop();
}
