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

#include "nfc_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hb_nfc_timer.h"
#include "nfc_card_info.h"
#include "nfc_poller.h"
#include "nfc_reader.h"
#include "poller.h"
#include "st25r3916_core.h"

static const char *TAG = "NFC_MANAGER";

static struct {
  nfc_manager_state_t state;
  nfc_manager_card_found_cb_t cb;
  void *ctx;
  TaskHandle_t task;
  bool running;
} s_mgr = {0};

static void nfc_manager_auto_handle(const hb_nfc_card_data_t *card) {
  if (card == NULL)
    return;

  switch (card->protocol) {
    case HB_PROTO_MF_CLASSIC:
      mf_classic_read_full((nfc_iso14443a_data_t *)&card->iso14443a);
      break;
    case HB_PROTO_MF_ULTRALIGHT:
      mful_dump_card((nfc_iso14443a_data_t *)&card->iso14443a);
      break;
    case HB_PROTO_MF_PLUS:
      mfp_probe_and_dump((nfc_iso14443a_data_t *)&card->iso14443a);
      break;
    case HB_PROTO_ISO14443_4A:
      t4t_dump_ndef((nfc_iso14443a_data_t *)&card->iso14443a);
      break;
    default:
      break;
  }
}

static void nfc_manager_task(void *arg) {
  (void)arg;
  s_mgr.state = NFC_MANAGER_STATE_SCANNING;

  while (s_mgr.running) {
    nfc_iso14443a_data_t card = {0};
    hb_nfc_err_t err = iso14443a_poller_select(&card);

    if (err == HB_NFC_OK) {
      s_mgr.state = NFC_MANAGER_STATE_READING;

      hb_nfc_card_data_t full = {0};
      full.protocol = HB_PROTO_ISO14443_3A;
      full.iso14443a = card;

      if (card.sak == NFC_SAK_ULTRALIGHT) {
        full.protocol = HB_PROTO_MF_ULTRALIGHT;
      } else if (card.sak == NFC_SAK_CLASSIC_1K || card.sak == NFC_SAK_CLASSIC_4K ||
                 card.sak == NFC_SAK_CLASSIC_MINI || card.sak == NFC_SAK_CLASSIC_1K_INF) {
        full.protocol = HB_PROTO_MF_CLASSIC;
      } else if (card.sak == NFC_SAK_PLUS_2K_SL2 || card.sak == NFC_SAK_PLUS_4K_SL2) {
        full.protocol = HB_PROTO_MF_CLASSIC;
      } else if (card.sak == NFC_SAK_ISO_DEP) {
        full.protocol = HB_PROTO_MF_PLUS;
      } else if (card.sak & NFC_SAK_ISO_DEP_BIT) {
        full.protocol = HB_PROTO_ISO14443_4A;
      }

      if (s_mgr.cb) {
        s_mgr.cb(&full, s_mgr.ctx);
      } else {
        nfc_manager_auto_handle(&full);
      }

      s_mgr.state = NFC_MANAGER_STATE_SCANNING;
    }

    hb_nfc_timer_delay_ms(100);
  }

  s_mgr.state = NFC_MANAGER_STATE_IDLE;
  s_mgr.task = NULL;
  vTaskDelete(NULL);
}

hb_nfc_err_t nfc_manager_start(nfc_manager_card_found_cb_t cb, void *ctx) {
  hb_nfc_err_t err = nfc_poller_start();
  if (err != HB_NFC_OK)
    return err;

  s_mgr.cb = cb;
  s_mgr.ctx = ctx;
  s_mgr.running = true;

  xTaskCreate(nfc_manager_task, "nfc_mgr", 8192, NULL, 5, &s_mgr.task);
  return HB_NFC_OK;
}

void nfc_manager_stop(void) {
  s_mgr.running = false;
  while (s_mgr.task != NULL) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  nfc_poller_stop();
}

nfc_manager_state_t nfc_manager_get_state(void) {
  return s_mgr.state;
}
