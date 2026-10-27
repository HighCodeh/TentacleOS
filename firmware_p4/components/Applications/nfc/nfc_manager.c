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

#include "nfc_manager.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "power_manager.h"
#include "sys_prio.h"

#include "hb_nfc_timer.h"
#include "felica.h"
#include "iso14443b.h"
#include "iso15693.h"
#include "iso_dep.h"
#include "led_control.h"
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
  bool no_sleep_held;
} s_mgr = {0};

static void nfc_manager_capture_rf_metrics(hb_nfc_card_data_t *card) {
  uint16_t regulator_mv = 0;
  bool current_limit = false;
  bool manual = false;
  const esp_err_t err = highboy_nfc_get_regulator(&regulator_mv, &current_limit, &manual, NULL);

  card->rf.amplitude = highboy_nfc_measure_amplitude();
  card->rf.regulator_mv = err == ESP_OK ? regulator_mv : 0;
  card->rf.regulator_manual = err == ESP_OK && manual;
  card->rf.current_limit = err == ESP_OK && current_limit;
  card->rf.valid = err == ESP_OK || card->rf.amplitude != 0;
}

static hb_nfc_protocol_t nfc_manager_classify_type_a(const nfc_iso14443a_data_t *card) {
  const nfc_card_type_info_t info = nfc_card_info_identify(card->sak, card->atqa);
  if (card->sak == NFC_SAK_PLUS_2K_SL2 || card->sak == NFC_SAK_PLUS_4K_SL2)
    return HB_PROTO_MF_PLUS;
  if (info.is_mf_classic)
    return HB_PROTO_MF_CLASSIC;
  if (info.is_mf_ultralight)
    return HB_PROTO_MF_ULTRALIGHT;
  if (info.is_iso_dep)
    return HB_PROTO_ISO14443_4A;
  return HB_PROTO_ISO14443_3A;
}

static esp_err_t discovery_error(hb_nfc_err_t error) {
  return error == HB_NFC_ERR_NO_CARD || error == HB_NFC_ERR_TIMEOUT || error == ESP_ERR_NOT_FOUND
             ? ESP_ERR_NOT_FOUND
             : error;
}

static esp_err_t nfc_manager_probe_a(hb_nfc_card_data_t *full) {
  hb_nfc_err_t error = nfc_poller_start();
  if (error != HB_NFC_OK)
    return error;

  nfc_iso14443a_data_t card = {0};
  error = iso14443a_poller_select(&card);
  if (error != HB_NFC_OK)
    return discovery_error(error);

  full->iso14443a = card;
  full->protocol = nfc_manager_classify_type_a(&card);

  /* SAK 0x20 only proves ISO-DEP support. It does not identify MIFARE Plus,
   * DESFire, EMV, JavaCard, or a Type 4 NDEF product. Preserve that distinction
   * and collect ATS as activation evidence without making product guesses. */
  if (full->protocol == HB_PROTO_ISO14443_4A) {
    nfc_iso_dep_data_t dep = {0};
    if (iso_dep_rats(8, 0, &dep) == HB_NFC_OK)
      full->data.iso_dep = dep;
  }
  return ESP_OK;
}

static esp_err_t nfc_manager_probe_b(hb_nfc_card_data_t *full) {
  hb_nfc_err_t error = iso14443b_poller_init();
  if (error != HB_NFC_OK)
    return error;
  nfc_iso14443b_data_t card = {0};
  error = iso14443b_reqb(NFC_REQB_AFI_ALL, NFC_REQB_PARAM_DEFAULT, &card);
  if (error != HB_NFC_OK)
    return discovery_error(error);

  full->data.iso14443b = card;
  full->protocol =
      iso14443b_attrib(&card, 8, 0) == HB_NFC_OK ? HB_PROTO_ISO14443_4B : HB_PROTO_ISO14443_3B;
  return ESP_OK;
}

static esp_err_t nfc_manager_probe_f(hb_nfc_card_data_t *full) {
  hb_nfc_err_t error = felica_poller_init();
  if (error != HB_NFC_OK)
    return error;
  felica_tag_t tag = {0};
  error = felica_sensf_req(FELICA_SC_COMMON, &tag);
  if (error != HB_NFC_OK)
    return discovery_error(error);

  full->protocol = HB_PROTO_FELICA;
  memcpy(full->data.felica.idm, tag.idm, sizeof(tag.idm));
  memcpy(full->data.felica.pmm, tag.pmm, sizeof(tag.pmm));
  memcpy(full->data.felica.system_code, tag.rd, sizeof(tag.rd));
  full->data.felica.system_code_valid = tag.rd_valid;
  return ESP_OK;
}

static esp_err_t nfc_manager_probe_v(hb_nfc_card_data_t *full) {
  hb_nfc_err_t error = iso15693_poller_init();
  if (error != HB_NFC_OK)
    return error;
  iso15693_tag_t tag = {0};
  error = iso15693_inventory(&tag);
  if (error != HB_NFC_OK)
    return discovery_error(error);

  /* Geometry is optional. A valid inventory is still a valid card read. */
  (void)iso15693_get_system_info(&tag);
  full->protocol = HB_PROTO_ISO15693;
  memcpy(full->data.iso15693.uid, tag.uid, sizeof(tag.uid));
  full->data.iso15693.dsfid = tag.dsfid;
  full->data.iso15693.block_count = tag.block_count;
  full->data.iso15693.block_size = tag.block_size;
  return ESP_OK;
}

static esp_err_t nfc_manager_probe(nfc_manager_tech_t tech, hb_nfc_card_data_t *full) {
  memset(full, 0, sizeof(*full));
  full->protocol = HB_PROTO_UNKNOWN;
  switch (tech) {
    case NFC_MANAGER_TECH_A:
      return nfc_manager_probe_a(full);
    case NFC_MANAGER_TECH_B:
      return nfc_manager_probe_b(full);
    case NFC_MANAGER_TECH_F:
      return nfc_manager_probe_f(full);
    case NFC_MANAGER_TECH_V:
      return nfc_manager_probe_v(full);
    default:
      return ESP_ERR_INVALID_ARG;
  }
}

esp_err_t nfc_manager_poll(nfc_manager_tech_t tech, hb_nfc_card_data_t *out_card) {
  if (out_card == NULL || tech < 0 || tech >= NFC_MANAGER_TECH_COUNT)
    return ESP_ERR_INVALID_ARG;
  return nfc_manager_probe(tech, out_card);
}

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
  nfc_manager_tech_t tech = NFC_MANAGER_TECH_A;

  while (s_mgr.running) {
    hb_nfc_card_data_t full = {0};
    if (nfc_manager_probe(tech, &full) == ESP_OK) {
      s_mgr.state = NFC_MANAGER_STATE_READING;
      nfc_manager_capture_rf_metrics(&full);

      led_signal_info(); // a card was detected and read

      if (s_mgr.cb) {
        s_mgr.cb(&full, s_mgr.ctx);
      } else {
        nfc_manager_auto_handle(&full);
      }

      s_mgr.state = NFC_MANAGER_STATE_SCANNING;
    }

    tech = (nfc_manager_tech_t)((tech + 1) % NFC_MANAGER_TECH_COUNT);
    hb_nfc_timer_delay_ms(25);
  }

  s_mgr.state = NFC_MANAGER_STATE_IDLE;
  s_mgr.task = NULL;
  vTaskDelete(NULL);
}

hb_nfc_err_t nfc_manager_start(nfc_manager_card_found_cb_t cb, void *ctx) {
  if (s_mgr.running || s_mgr.task != NULL)
    return HB_NFC_ERR_INTERNAL;

  esp_err_t pm_err = power_manager_no_sleep_acquire();
  if (pm_err != ESP_OK) {
    ESP_LOGE(TAG, "failed to acquire NFC no-sleep lock: %s", esp_err_to_name(pm_err));
    return HB_NFC_ERR_INTERNAL;
  }
  s_mgr.no_sleep_held = true;

  hb_nfc_err_t err = nfc_poller_start();
  if (err != HB_NFC_OK) {
    (void)power_manager_no_sleep_release();
    s_mgr.no_sleep_held = false;
    return err;
  }

  s_mgr.cb = cb;
  s_mgr.ctx = ctx;
  s_mgr.running = true;

  BaseType_t rc = xTaskCreatePinnedToCore(
      nfc_manager_task, "nfc_mgr", 8192, NULL, SYS_PRIO_SERVICE_HI, &s_mgr.task, SYS_CORE_RADIO);
  if (rc != pdPASS) {
    s_mgr.running = false;
    s_mgr.task = NULL;
    nfc_poller_stop();
    (void)power_manager_no_sleep_release();
    s_mgr.no_sleep_held = false;
    return HB_NFC_ERR_INTERNAL;
  }
  return HB_NFC_OK;
}

void nfc_manager_stop(void) {
  s_mgr.running = false;
  while (s_mgr.task != NULL) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  nfc_poller_stop();
  if (s_mgr.no_sleep_held) {
    esp_err_t err = power_manager_no_sleep_release();
    if (err != ESP_OK)
      ESP_LOGW(TAG, "failed to release NFC no-sleep lock: %s", esp_err_to_name(err));
    s_mgr.no_sleep_held = false;
  }
}

nfc_manager_state_t nfc_manager_get_state(void) {
  return s_mgr.state;
}
