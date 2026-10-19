// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "nfc_manager.h"

#define NFC_REQB_AFI_ALL 0
#define NFC_REQB_PARAM_DEFAULT 0
#define FELICA_SC_COMMON 0
static hb_nfc_err_t s_setup_error, s_discovery_error;
static unsigned s_setups;
typedef struct {uint8_t idm[8],pmm[8],rd[2]; bool rd_valid;} felica_tag_t;
typedef struct {uint8_t uid[8],dsfid; uint16_t block_count; uint8_t block_size;} iso15693_tag_t;
static hb_nfc_err_t setup(void) {++s_setups; return s_setup_error;}
static hb_nfc_err_t nfc_poller_start(void) {return setup();}
static hb_nfc_err_t iso14443b_poller_init(void) {return setup();}
static hb_nfc_err_t felica_poller_init(void) {return setup();}
static hb_nfc_err_t iso15693_poller_init(void) {return setup();}
static hb_nfc_err_t iso14443a_poller_select(nfc_iso14443a_data_t *out) {(void)out;return s_discovery_error;}
static hb_nfc_protocol_t nfc_manager_classify_type_a(const nfc_iso14443a_data_t *card) {(void)card;return HB_PROTO_ISO14443_3A;}
static hb_nfc_err_t iso_dep_rats(uint8_t fsdi,uint8_t cid,nfc_iso_dep_data_t *out) {(void)fsdi;(void)cid;(void)out;return ESP_FAIL;}
static hb_nfc_err_t iso14443b_reqb(uint8_t afi,uint8_t param,nfc_iso14443b_data_t *out) {(void)afi;(void)param;(void)out;return s_discovery_error;}
static hb_nfc_err_t iso14443b_attrib(nfc_iso14443b_data_t *card,uint8_t fsdi,uint8_t cid) {(void)card;(void)fsdi;(void)cid;return ESP_FAIL;}
static hb_nfc_err_t felica_sensf_req(uint16_t code,felica_tag_t *out) {(void)code;(void)out;return s_discovery_error;}
static hb_nfc_err_t iso15693_inventory(iso15693_tag_t *out) {(void)out;return s_discovery_error;}
static hb_nfc_err_t iso15693_get_system_info(iso15693_tag_t *tag) {(void)tag;return ESP_FAIL;}
/* NFC_POLL */
int main(void) {
  hb_nfc_card_data_t card;
  assert(nfc_manager_poll(NFC_MANAGER_TECH_A,NULL)==ESP_ERR_INVALID_ARG);
  assert(nfc_manager_poll(NFC_MANAGER_TECH_COUNT,&card)==ESP_ERR_INVALID_ARG);
  assert(s_setups==0);
  for(nfc_manager_tech_t tech=0;tech<NFC_MANAGER_TECH_COUNT;++tech) {
    s_setup_error=ESP_ERR_TIMEOUT; s_discovery_error=ESP_OK;
    assert(nfc_manager_poll(tech,&card)==ESP_ERR_TIMEOUT);
    s_setup_error=ESP_OK; s_discovery_error=HB_NFC_ERR_NO_CARD;
    assert(nfc_manager_poll(tech,&card)==ESP_ERR_NOT_FOUND);
    s_discovery_error=ESP_ERR_TIMEOUT;
    assert(nfc_manager_poll(tech,&card)==ESP_ERR_NOT_FOUND);
    s_discovery_error=ESP_FAIL;
    assert(nfc_manager_poll(tech,&card)==ESP_FAIL);
    s_discovery_error=ESP_OK;
    assert(nfc_manager_poll(tech,&card)==ESP_OK);
    assert(card.protocol!=HB_PROTO_UNKNOWN);
  }
  puts("PASS: production NFC probes preserve setup errors, normalize card absence and tolerate optional activation/geometry failure");
}
