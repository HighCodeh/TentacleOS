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

#include "mf_desfire_emu.h"

#include <string.h>

static const char *TAG = "NFC_DESFIRE_EMU";

#define DESFIRE_CLA             0x90
#define DESFIRE_CMD_GET_VERSION 0x60
#define DESFIRE_CMD_ADDITIONAL  0xAF
#define DESFIRE_CMD_SELECT_APP  0x5A
#define DESFIRE_CMD_GET_APP_IDS 0x6A
#define DESFIRE_CMD_GET_KEY_SET 0x45

#define DESFIRE_SW_OK   0x9100
#define DESFIRE_SW_MORE 0x91AF
#define DESFIRE_SW_ERR  0x91AE

#define DESFIRE_APDU_HDR_SIZE      5
#define DESFIRE_AID_SIZE           3
#define DESFIRE_DUMMY_KEY_SETTINGS 0x0F
#define DESFIRE_DUMMY_MAX_KEYS     0x10

static uint8_t s_ver_step = 0;
static uint8_t s_aid[DESFIRE_AID_SIZE] = {0x00, 0x00, 0x00};
static bool s_app_selected = false;

static const uint8_t k_uid7[7] = {0x04, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
static const uint8_t k_batch7[7] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x12, 0x26};

static void
resp_with_sw(uint8_t *out, int *out_len, const uint8_t *data, int data_len, uint16_t sw) {
  int pos = 0;
  if (data && data_len > 0) {
    memcpy(&out[pos], data, (size_t)data_len);
    pos += data_len;
  }
  out[pos++] = (uint8_t)((sw >> 8) & 0xFF);
  out[pos++] = (uint8_t)(sw & 0xFF);
  *out_len = pos;
}

void mf_desfire_emu_reset(void) {
  s_ver_step = 0;
  s_aid[0] = s_aid[1] = s_aid[2] = 0x00;
  s_app_selected = false;
}

static void parse_lc(const uint8_t *apdu, int apdu_len, const uint8_t **data, int *data_len) {
  *data = NULL;
  *data_len = 0;
  if (apdu_len < DESFIRE_APDU_HDR_SIZE)
    return;
  uint8_t lc = apdu[4];
  if (apdu_len >= DESFIRE_APDU_HDR_SIZE + lc && lc > 0) {
    *data = &apdu[DESFIRE_APDU_HDR_SIZE];
    *data_len = lc;
  }
}

bool mf_desfire_emu_handle_apdu(const uint8_t *apdu, int apdu_len, uint8_t *out, int *out_len) {
  if (apdu == NULL || apdu_len < 2 || out == NULL || out_len == NULL)
    return false;
  if (apdu[0] != DESFIRE_CLA)
    return false;

  const uint8_t *data = NULL;
  int data_len = 0;
  parse_lc(apdu, apdu_len, &data, &data_len);

  uint8_t cmd = apdu[1];
  switch (cmd) {
    case DESFIRE_CMD_GET_VERSION: {
      static const uint8_t ver_hw[7] = {0x04, 0x01, 0x01, 0x01, 0x00, 0x1A, 0x05};
      s_ver_step = 1;
      resp_with_sw(out, out_len, ver_hw, sizeof(ver_hw), DESFIRE_SW_MORE);
      return true;
    }
    case DESFIRE_CMD_ADDITIONAL: {
      if (s_ver_step == 1) {
        static const uint8_t ver_sw[7] = {0x04, 0x01, 0x01, 0x01, 0x00, 0x1A, 0x05};
        s_ver_step = 2;
        resp_with_sw(out, out_len, ver_sw, sizeof(ver_sw), DESFIRE_SW_MORE);
        return true;
      }
      if (s_ver_step == 2) {
        s_ver_step = 3;
        resp_with_sw(out, out_len, k_uid7, sizeof(k_uid7), DESFIRE_SW_MORE);
        return true;
      }
      if (s_ver_step == 3) {
        s_ver_step = 0;
        resp_with_sw(out, out_len, k_batch7, sizeof(k_batch7), DESFIRE_SW_OK);
        return true;
      }
      resp_with_sw(out, out_len, NULL, 0, DESFIRE_SW_ERR);
      return true;
    }
    case DESFIRE_CMD_SELECT_APP: {
      if (data_len == DESFIRE_AID_SIZE && data) {
        memcpy(s_aid, data, DESFIRE_AID_SIZE);
        s_app_selected = true;
        resp_with_sw(out, out_len, NULL, 0, DESFIRE_SW_OK);
      } else {
        resp_with_sw(out, out_len, NULL, 0, DESFIRE_SW_ERR);
      }
      return true;
    }
    case DESFIRE_CMD_GET_APP_IDS: {
      resp_with_sw(out, out_len, NULL, 0, DESFIRE_SW_OK);
      return true;
    }
    case DESFIRE_CMD_GET_KEY_SET: {
      uint8_t ks[2] = {DESFIRE_DUMMY_KEY_SETTINGS, DESFIRE_DUMMY_MAX_KEYS};
      (void)s_app_selected;
      resp_with_sw(out, out_len, ks, sizeof(ks), DESFIRE_SW_OK);
      return true;
    }
    default:
      resp_with_sw(out, out_len, NULL, 0, DESFIRE_SW_ERR);
      return true;
  }
}
