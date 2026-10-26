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

#include "nfc_protocol_service.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "felica.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "highboy_nfc.h"
#include "iso15693.h"
#include "iso_dep.h"
#include "mf_desfire.h"
#include "mf_ultralight.h"
#include "ndef.h"
#include "nfc_poller.h"
#include "poller.h"
#include "power_manager.h"
#include "sys_prio.h"
#include "t4t.h"

#define PROTOCOL_TASK_STACK 8192
#define UL_MAX_PAGES        135
#define NDEF_MAX_BYTES      1024

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static nfc_protocol_status_t s_status;
static bool s_cancel;

static void add_line(nfc_protocol_result_t *result, const char *format, ...) {
  if (result->line_count >= NFC_PROTOCOL_RESULT_LINES)
    return;
  va_list args;
  va_start(args, format);
  vsnprintf(result->lines[result->line_count], NFC_PROTOCOL_RESULT_LINE_LEN, format, args);
  va_end(args);
  result->line_count++;
}

static bool cancel_requested(void) {
  bool cancel;
  portENTER_CRITICAL(&s_mux);
  cancel = s_cancel;
  portEXIT_CRITICAL(&s_mux);
  return cancel;
}

static void format_uid(const uint8_t *uid, uint8_t uid_len, bool reverse, char *out, size_t max) {
  size_t pos = 0;
  if (out == NULL || max == 0)
    return;
  out[0] = '\0';
  for (uint8_t n = 0; n < uid_len; n++) {
    const uint8_t i = reverse ? (uint8_t)(uid_len - 1U - n) : n;
    if (pos + 4 >= max)
      break;
    pos += (size_t)snprintf(out + pos, max - pos, n == 0 ? "%02X" : ":%02X", uid[i]);
  }
}

static hb_nfc_err_t select_type_a(nfc_iso14443a_data_t *card) {
  if (cancel_requested())
    return ESP_ERR_INVALID_STATE;
  return iso14443a_poller_select(card);
}

static hb_nfc_err_t read_ultralight(nfc_protocol_result_t *result) {
  nfc_iso14443a_data_t card = {0};
  hb_nfc_err_t err = select_type_a(&card);
  if (err != HB_NFC_OK)
    return err;
  if (card.sak != 0x00)
    return HB_NFC_ERR_PROTOCOL;

  char uid[32];
  format_uid(card.uid, card.uid_len, false, uid, sizeof(uid));
  strlcpy(result->heading, "Ultralight / NTAG", sizeof(result->heading));
  add_line(result, "UID  %s", uid);
  uint8_t version[8] = {0};
  const int version_len = mful_get_version(version);
  if (version_len >= 8)
    add_line(result,
             "Version  %02X %02X %02X %02X %02X %02X %02X %02X",
             version[0],
             version[1],
             version[2],
             version[3],
             version[4],
             version[5],
             version[6],
             version[7]);
  else
    add_line(result, "GET_VERSION not supported");

  uint8_t memory[UL_MAX_PAGES * 4] = {0};
  const int pages = mful_read_all(memory, UL_MAX_PAGES);
  if (pages <= 0)
    return HB_NFC_ERR_PROTOCOL;
  add_line(result, "Readable pages  %d (%d bytes)", pages, pages * 4);
  const int shown = pages < 5 ? pages : 5;
  for (int page = 0; page < shown; page++)
    add_line(result,
             "P%03d  %02X %02X %02X %02X",
             page,
             memory[page * 4],
             memory[page * 4 + 1],
             memory[page * 4 + 2],
             memory[page * 4 + 3]);
  if (pages > shown)
    add_line(result, "... %d more readable pages", pages - shown);
  return HB_NFC_OK;
}

static void
format_ndef_records(const uint8_t *ndef, size_t ndef_len, nfc_protocol_result_t *result) {
  ndef_parser_t parser;
  ndef_parser_init(&parser, ndef, ndef_len);
  ndef_record_t record;
  int record_count = 0;
  while (result->line_count < NFC_PROTOCOL_RESULT_LINES && ndef_parse_next(&parser, &record)) {
    record_count++;
    if (ndef_record_is_text(&record)) {
      ndef_text_t text;
      if (ndef_decode_text(&record, &text)) {
        char value[42];
        const size_t copy = text.text_len < sizeof(value) - 1 ? text.text_len : sizeof(value) - 1;
        memcpy(value, text.text, copy);
        value[copy] = '\0';
        add_line(result, "R%d Text  %s", record_count, value);
        continue;
      }
    }
    if (ndef_record_is_uri(&record)) {
      char uri[48];
      size_t uri_len = 0;
      if (ndef_decode_uri(&record, uri, sizeof(uri), &uri_len)) {
        add_line(result, "R%d URI  %s", record_count, uri);
        continue;
      }
    }
    if (ndef_record_is_mime(&record)) {
      char type[32];
      const size_t copy = record.type_len < sizeof(type) - 1 ? record.type_len : sizeof(type) - 1;
      memcpy(type, record.type, copy);
      type[copy] = '\0';
      add_line(result, "R%d MIME %s (%u B)", record_count, type, (unsigned)record.payload_len);
    } else {
      add_line(result,
               "R%d TNF %u type %u payload %u B",
               record_count,
               record.tnf,
               record.type_len,
               (unsigned)record.payload_len);
    }
  }
  if (record_count == 0)
    add_line(result, "No valid NDEF records");
}

static hb_nfc_err_t read_ndef(nfc_protocol_result_t *result) {
  nfc_iso14443a_data_t card = {0};
  hb_nfc_err_t err = select_type_a(&card);
  if (err != HB_NFC_OK)
    return err;
  char uid[32];
  format_uid(card.uid, card.uid_len, false, uid, sizeof(uid));
  strlcpy(result->heading, "NDEF records", sizeof(result->heading));
  add_line(result, "UID  %s", uid);

  uint8_t ndef[NDEF_MAX_BYTES] = {0};
  size_t ndef_len = 0;
  if ((card.sak & 0x20U) != 0U) {
    nfc_iso_dep_data_t dep = {0};
    err = iso_dep_rats(8, 0, &dep);
    if (err != HB_NFC_OK)
      return err;
    t4t_cc_t cc = {0};
    err = t4t_read_cc(&dep, &cc);
    if (err != HB_NFC_OK)
      return err;
    err = t4t_read_ndef(&dep, &cc, ndef, sizeof(ndef), &ndef_len);
    if (err != HB_NFC_OK)
      return err;
    add_line(result, "Type 4 Tag  %u bytes", (unsigned)ndef_len);
  } else if (card.sak == 0x00) {
    uint8_t memory[UL_MAX_PAGES * 4] = {0};
    const int pages = mful_read_all(memory, UL_MAX_PAGES);
    if (pages <= 4)
      return HB_NFC_ERR_PROTOCOL;
    ndef_tlv_t tlv;
    if (ndef_tlv_find_ndef(&memory[16], (size_t)pages * 4U - 16U, &tlv) != NDEF_TLV_STATUS_OK)
      return HB_NFC_ERR_NOT_FOUND;
    ndef_len = tlv.length < sizeof(ndef) ? tlv.length : sizeof(ndef);
    memcpy(ndef, tlv.value, ndef_len);
    add_line(result, "Type 2 Tag  %u bytes", (unsigned)tlv.length);
  } else {
    return HB_NFC_ERR_PROTOCOL;
  }
  format_ndef_records(ndef, ndef_len, result);
  return HB_NFC_OK;
}

static hb_nfc_err_t read_desfire(nfc_protocol_result_t *result) {
  nfc_iso14443a_data_t card = {0};
  hb_nfc_err_t err = select_type_a(&card);
  if (err != HB_NFC_OK)
    return err;
  if ((card.sak & 0x20U) == 0U)
    return HB_NFC_ERR_PROTOCOL;

  char uid[32];
  format_uid(card.uid, card.uid_len, false, uid, sizeof(uid));
  strlcpy(result->heading, "DESFire probe", sizeof(result->heading));
  add_line(result, "UID  %s", uid);

  mf_desfire_session_t session = {0};
  err = mf_desfire_poller_init(&card, &session);
  if (err != HB_NFC_OK)
    return err;
  add_line(result, "ISO-DEP ATS %u B  FSC %u", session.dep.ats_len, session.dep.fsc);

  mf_desfire_version_t version = {0};
  err = mf_desfire_get_version(&session, &version);
  if (err != HB_NFC_OK)
    return err;
  add_line(
      result, "HW %u.%u  SW %u.%u", version.hw[3], version.hw[4], version.sw[3], version.sw[4]);
  add_line(
      result, "Vendor %02X type %02X size-code %02X", version.hw[0], version.hw[1], version.hw[5]);
  uint8_t apps[96] = {0};
  size_t apps_len = 0;
  hb_nfc_err_t apps_err = mf_desfire_get_application_ids(&session, apps, sizeof(apps), &apps_len);
  if (apps_err == HB_NFC_OK) {
    add_line(result, "Applications  %u", (unsigned)(apps_len / 3U));
    for (size_t i = 0; i + 2 < apps_len && i < 12; i += 3)
      add_line(result, "AID  %02X%02X%02X", apps[i], apps[i + 1], apps[i + 2]);
  } else {
    add_line(result, "Application list unavailable");
  }
  add_line(result, "No authentication attempted");
  return HB_NFC_OK;
}

static hb_nfc_err_t read_iso15693(nfc_protocol_result_t *result) {
  hb_nfc_err_t err = iso15693_poller_init();
  if (err != HB_NFC_OK)
    return err;
  iso15693_tag_t tag = {0};
  err = iso15693_inventory(&tag);
  if (err != HB_NFC_OK)
    return err;
  char uid[32];
  format_uid(tag.uid, ISO15693_UID_LEN, true, uid, sizeof(uid));
  strlcpy(result->heading, "ISO15693 / NFC-V", sizeof(result->heading));
  add_line(result, "UID  %s", uid);
  err = iso15693_get_system_info(&tag);
  if (err != HB_NFC_OK) {
    add_line(result, "System info unavailable");
    return HB_NFC_OK;
  }
  add_line(result, "Geometry  %u x %u bytes", tag.block_count, tag.block_size);
  add_line(result, "AFI %02X  DSFID %02X  IC %02X", tag.afi, tag.dsfid, tag.ic_ref);
  const uint16_t blocks = tag.block_count < 5 ? tag.block_count : 5;
  for (uint16_t block = 0; block < blocks; block++) {
    uint8_t data[ISO15693_MAX_BLOCK_SIZE] = {0};
    size_t data_len = 0;
    hb_nfc_err_t block_err =
        iso15693_read_single_block(&tag, (uint8_t)block, data, sizeof(data), &data_len);
    if (block_err != HB_NFC_OK) {
      add_line(result, "B%03u  read error", block);
      continue;
    }
    add_line(result,
             "B%03u  %02X %02X %02X %02X%s",
             block,
             data_len > 0 ? data[0] : 0,
             data_len > 1 ? data[1] : 0,
             data_len > 2 ? data[2] : 0,
             data_len > 3 ? data[3] : 0,
             data_len > 4 ? " ..." : "");
  }
  return HB_NFC_OK;
}

static hb_nfc_err_t read_felica(nfc_protocol_result_t *result) {
  hb_nfc_err_t err = felica_poller_init();
  if (err != HB_NFC_OK)
    return err;
  felica_tag_t tag = {0};
  err = felica_sensf_req(FELICA_SC_COMMON, &tag);
  if (err != HB_NFC_OK)
    return err;
  char idm[32], pmm[32];
  format_uid(tag.idm, FELICA_IDM_LEN, false, idm, sizeof(idm));
  format_uid(tag.pmm, FELICA_PMM_LEN, false, pmm, sizeof(pmm));
  strlcpy(result->heading, "FeliCa / NFC-F", sizeof(result->heading));
  add_line(result, "IDm  %s", idm);
  add_line(result, "PMm  %s", pmm);
  if (tag.rd_valid)
    add_line(result, "System / RD  %02X%02X", tag.rd[0], tag.rd[1]);
  uint8_t blocks[2 * FELICA_BLOCK_SIZE] = {0};
  hb_nfc_err_t read_err = felica_read_blocks(&tag, FELICA_SERVICE_COMMON_RO, 0, 2, blocks);
  if (read_err == HB_NFC_OK) {
    add_line(result, "Svc 000B block 0");
    add_line(result,
             "%02X %02X %02X %02X %02X %02X %02X %02X ...",
             blocks[0],
             blocks[1],
             blocks[2],
             blocks[3],
             blocks[4],
             blocks[5],
             blocks[6],
             blocks[7]);
    add_line(result, "Svc 000B block 1");
    add_line(result,
             "%02X %02X %02X %02X %02X %02X %02X %02X ...",
             blocks[16],
             blocks[17],
             blocks[18],
             blocks[19],
             blocks[20],
             blocks[21],
             blocks[22],
             blocks[23]);
  } else {
    add_line(result, "Common RO service not readable");
  }
  return HB_NFC_OK;
}

static void protocol_task(void *arg) {
  const nfc_protocol_op_t operation = (nfc_protocol_op_t)(uintptr_t)arg;
  nfc_protocol_result_t result = {0};
  hb_nfc_err_t err = HB_NFC_ERR_INTERNAL;
  bool no_sleep = false;
  bool field = false;

  if (!highboy_nfc_is_initialized()) {
    highboy_nfc_config_t config = HIGHBOY_NFC_CONFIG_DEFAULT();
    if (highboy_nfc_init(&config) != ESP_OK)
      goto done;
  }
  if (power_manager_no_sleep_acquire() != ESP_OK)
    goto done;
  no_sleep = true;
  err = nfc_poller_start();
  if (err != HB_NFC_OK)
    goto done;
  field = true;
  vTaskDelay(pdMS_TO_TICKS(100));
  if (cancel_requested())
    goto done;

  switch (operation) {
    case NFC_PROTOCOL_OP_DESFIRE:
      err = read_desfire(&result);
      break;
    case NFC_PROTOCOL_OP_ISO15693:
      err = read_iso15693(&result);
      break;
    case NFC_PROTOCOL_OP_ULTRALIGHT:
      err = read_ultralight(&result);
      break;
    case NFC_PROTOCOL_OP_NDEF:
      err = read_ndef(&result);
      break;
    case NFC_PROTOCOL_OP_FELICA:
      err = read_felica(&result);
      break;
    default:
      err = HB_NFC_ERR_PARAM;
      break;
  }

done:
  if (field)
    nfc_poller_stop();
  else if (highboy_nfc_is_initialized())
    (void)highboy_nfc_field_off();
  if (no_sleep)
    (void)power_manager_no_sleep_release();

  const bool cancelled = cancel_requested();
  portENTER_CRITICAL(&s_mux);
  s_status.result = result;
  s_status.error = err;
  s_status.running = false;
  s_status.state = cancelled          ? NFC_PROTOCOL_STATE_CANCELLED
                   : err == HB_NFC_OK ? NFC_PROTOCOL_STATE_DONE
                                      : NFC_PROTOCOL_STATE_ERROR;
  portEXIT_CRITICAL(&s_mux);
  vTaskDelete(NULL);
}

hb_nfc_err_t nfc_protocol_service_start(nfc_protocol_op_t operation) {
  if ((int)operation < 0 || operation >= NFC_PROTOCOL_OP_COUNT)
    return HB_NFC_ERR_PARAM;
  portENTER_CRITICAL(&s_mux);
  if (s_status.running) {
    portEXIT_CRITICAL(&s_mux);
    return ESP_ERR_INVALID_STATE;
  }
  memset(&s_status, 0, sizeof(s_status));
  s_status.operation = operation;
  s_status.state = NFC_PROTOCOL_STATE_RUNNING;
  s_status.running = true;
  s_cancel = false;
  portEXIT_CRITICAL(&s_mux);
  if (xTaskCreatePinnedToCore(protocol_task,
                              "nfc_protocol",
                              PROTOCOL_TASK_STACK,
                              (void *)(uintptr_t)operation,
                              SYS_PRIO_SERVICE_HI,
                              NULL,
                              SYS_CORE_RADIO) != pdPASS) {
    portENTER_CRITICAL(&s_mux);
    s_status.running = false;
    s_status.state = NFC_PROTOCOL_STATE_ERROR;
    s_status.error = HB_NFC_ERR_INTERNAL;
    portEXIT_CRITICAL(&s_mux);
    return HB_NFC_ERR_INTERNAL;
  }
  return HB_NFC_OK;
}

void nfc_protocol_service_cancel(void) {
  portENTER_CRITICAL(&s_mux);
  s_cancel = true;
  portEXIT_CRITICAL(&s_mux);
}

void nfc_protocol_service_stop(void) {
  nfc_protocol_service_cancel();
  for (;;) {
    portENTER_CRITICAL(&s_mux);
    const bool running = s_status.running;
    portEXIT_CRITICAL(&s_mux);
    if (!running)
      break;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void nfc_protocol_service_get_status(nfc_protocol_status_t *out) {
  if (out == NULL)
    return;
  portENTER_CRITICAL(&s_mux);
  *out = s_status;
  portEXIT_CRITICAL(&s_mux);
}

const char *nfc_protocol_operation_name(nfc_protocol_op_t operation) {
  switch (operation) {
    case NFC_PROTOCOL_OP_DESFIRE:
      return "DESFire";
    case NFC_PROTOCOL_OP_ISO15693:
      return "NFC-V / ISO15693";
    case NFC_PROTOCOL_OP_ULTRALIGHT:
      return "Ultralight / NTAG";
    case NFC_PROTOCOL_OP_NDEF:
      return "NDEF";
    case NFC_PROTOCOL_OP_FELICA:
      return "FeliCa";
    default:
      return "NFC";
  }
}
