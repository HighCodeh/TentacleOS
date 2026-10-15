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
#include "nfc_scanner.h"

#include <stdlib.h>

#include "esp_log.h"

#include "felica.h"
#include "iso14443b.h"
#include "iso15693.h"
#include "nfc_card_info.h"
#include "nfc_poller.h"
#include "poller.h"

static const char *TAG = "NFC_SCANNER";

struct nfc_scanner {
  nfc_scanner_cb_t cb;
  void *ctx;
  bool running;
};

nfc_scanner_t *nfc_scanner_alloc(void) {
  nfc_scanner_t *s = calloc(1, sizeof(nfc_scanner_t));
  if (s == NULL) {
    ESP_LOGE(TAG, "Failed to allocate nfc_scanner_t");
    return NULL;
  }
  return s;
}

void nfc_scanner_free(nfc_scanner_t *s) {
  if (s) {
    nfc_scanner_stop(s);
    free(s);
  }
}

hb_nfc_err_t nfc_scanner_start(nfc_scanner_t *s, nfc_scanner_cb_t cb, void *ctx) {
  if (s == NULL || cb == NULL)
    return HB_NFC_ERR_PARAM;
  s->cb = cb;
  s->ctx = ctx;
  s->running = true;

  nfc_scanner_event_t evt = {0};
  hb_nfc_err_t result = HB_NFC_ERR_NO_CARD;

  /* Step 1: NFC-A (ISO 14443-3A) */
  nfc_iso14443a_data_t card_a = {0};
  if (iso14443a_poller_select(&card_a) == HB_NFC_OK) {
    ESP_LOGI(TAG, "NFC-A found UID len=%u SAK=0x%02X", card_a.uid_len, card_a.sak);
    evt.protocols[evt.count++] = HB_PROTO_ISO14443_3A;
    nfc_card_type_info_t info = nfc_card_info_identify(card_a.sak, card_a.atqa);
    if (card_a.sak == NFC_SAK_PLUS_2K_SL2 || card_a.sak == NFC_SAK_PLUS_4K_SL2)
      evt.protocols[evt.count++] = HB_PROTO_MF_PLUS;
    else if (info.is_mf_classic)
      evt.protocols[evt.count++] = HB_PROTO_MF_CLASSIC;
    else if (info.is_mf_ultralight)
      evt.protocols[evt.count++] = HB_PROTO_MF_ULTRALIGHT;
    else if (info.is_iso_dep)
      evt.protocols[evt.count++] = HB_PROTO_ISO14443_4A;
    result = HB_NFC_OK;
  }

  /* Step 2: NFC-B (ISO 14443-3B) */
  iso14443b_poller_init();
  {
    nfc_iso14443b_data_t card_b = {0};
    if (iso14443b_reqb(NFC_REQB_AFI_ALL, NFC_REQB_PARAM_DEFAULT, &card_b) == HB_NFC_OK) {
      ESP_LOGI(TAG,
               "NFC-B found PUPI=%02X%02X%02X%02X",
               card_b.pupi[0],
               card_b.pupi[1],
               card_b.pupi[2],
               card_b.pupi[3]);
      evt.protocols[evt.count++] = HB_PROTO_ISO14443_3B;
      if (iso14443b_attrib(&card_b, 8, 0) == HB_NFC_OK)
        evt.protocols[evt.count++] = HB_PROTO_ISO14443_4B;
      result = HB_NFC_OK;
    }
  }

  /* Step 3: NFC-F (FeliCa) */
  felica_poller_init();
  {
    felica_tag_t tag_f = {0};
    if (felica_sensf_req(FELICA_SC_COMMON, &tag_f) == HB_NFC_OK) {
      ESP_LOGI(TAG,
               "NFC-F found IDm=%02X%02X%02X%02X...",
               tag_f.idm[0],
               tag_f.idm[1],
               tag_f.idm[2],
               tag_f.idm[3]);
      evt.protocols[evt.count++] = HB_PROTO_FELICA;
      result = HB_NFC_OK;
    }
  }

  /* Step 4: NFC-V (ISO 15693) */
  iso15693_poller_init();
  {
    iso15693_tag_t tag_v = {0};
    if (iso15693_inventory(&tag_v) == HB_NFC_OK) {
      ESP_LOGI(TAG,
               "NFC-V found UID=%02X%02X%02X%02X...",
               tag_v.uid[0],
               tag_v.uid[1],
               tag_v.uid[2],
               tag_v.uid[3]);
      evt.protocols[evt.count++] = HB_PROTO_ISO15693;
      result = HB_NFC_OK;
    }
  }

  /* Restore NFC-A mode as default regardless of outcome. */
  nfc_poller_start();

  s->running = false;
  if (result == HB_NFC_OK)
    cb(evt, ctx);
  return result;
}

void nfc_scanner_stop(nfc_scanner_t *s) {
  if (s)
    s->running = false;
}
