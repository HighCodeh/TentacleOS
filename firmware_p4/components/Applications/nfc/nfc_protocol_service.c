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
#include "nfc_v_file.h"
#include "nfc_archive.h"
#include "nfc_desfire.h"
#include "nfc_ntag4xx.h"
#include "nfc_t4t.h"
#include "nfc_felica.h"
#include "felica_emu.h"
#include "nfc_t2t_file.h"
#include "nfc_plus_poller.h"
#include "nfc_dict_loader.h"
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "felica.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "highboy_nfc.h"
#include "iso15693.h"
#include "iso14443b.h"
#include "iso_dep.h"
#include "mf_desfire.h"
#include "mf_ultralight.h"
#include "ndef.h"
#include "nfc_poller.h"
#include "poller.h"
#include "power_manager.h"
#include "sys_prio.h"
#include "t4t.h"
#include "texkom.h"
#include "slix.h"
#include "nfc_store.h"
#include "hb_nfc_spi.h"
#include "st25r3916_reg.h"
#include "iso15693_listener.h"
#include "hb_nfc_v_rx.h"
#include "rf_frontend.h"
#include "st25r3916_cmd.h"

#define DESFIRE_FILE_LIMIT 10000U
#define DESFIRE_RETRY_MS   10U

#define PLUS_MAX_KEYS           64U
#define PLUS_PATH_SIZE          128U
#define NFC_V_RX_CONF1          0x13U
#define NFC_V_RX_CONF2          0x2DU
#define NFC_V_CORR_CONF1        0x13U
#define NFC_V_CORR_CONF2        0x01U
#define NFC_V_RETRY_MS          1U
#define NFC_V_CLEANUP_MS        10U
#define TEXKOM_PASSIVE_MODE     0x88U
#define TEXKOM_PASSIVE_CONTROL  0xC3U
#define TEXKOM_EMULATION_GAP_MS 3U
#define TEXKOM_READ_ATTEMPTS    100U
#define TEXKOM_RETRY_DELAY_MS   10U
#define PROTOCOL_TASK_STACK     8192
#define UL_AES_DUMP_DIR         "/sdcard/nfc/dumps"
#define UL_AES_PATH_SIZE        128U
#define UL_AES_COPY_LIMIT       1000U
#define UL_AES_PAGE_SIZE        4U
#define UL_MAX_PAGES            492
#define UL_LEGACY_PAGES         16
#define UL_VERSION_SIZE         8
#define NDEF_MAX_BYTES          1024
#define NFC_V_DUMP_HEADER_SIZE  8U
#define NFC_V_DUMP_VERSION      1U
#define NFC_V_BYTE_BITS         8U
#define ST25TB_WRITE_UID_SIZE   8U
#define ST25TB_WRITE_DATA_SIZE  4U
#define ST25TB_WRITE_INPUT_SIZE 11U
#define ST25TB_WRITE_SEPARATOR  2U
#define ST25TB_CMD_WRITE        0x09U
#define ST25TB_PROGRAM_MS       7U
#define HEX_DIGITS_PER_BYTE     2U
#define HEX_NIBBLE_BITS         4U
#define HEX_ALPHA_VALUE         10U

static const char *TAG = "NFC_PROTOCOL";

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static nfc_protocol_status_t s_status;
static bool s_cancel;
static char s_archive_path[NFC_ARCHIVE_PATH_SIZE];
EXT_RAM_BSS_ATTR static nfc_plus_file_t s_plus_card;
static uint8_t s_plus_key[MFP_BLOCK_SIZE];
static bool s_has_plus_key;
static bool s_plus_write_pending;
static bool s_plus_write_encrypted = true;
static char s_plus_last_path[PLUS_PATH_SIZE];
static char s_plus_write_path[PLUS_PATH_SIZE];
static texkom_data_t s_texkom_last;
static bool s_has_texkom;
EXT_RAM_BSS_ATTR static iso15693_listener_card_t s_v_card;
static iso15693_listener_t s_v_listener;
static bool s_has_v;
static bool s_v_result_valid;
static bool s_has_ul_aes_key;
static bool s_ul_aes_secure;
static uint8_t s_ul_aes_key[T2T_AES_BLOCK_SIZE];
static char s_aes_last_path[UL_AES_PATH_SIZE];
static char s_aes_write_path[UL_AES_PATH_SIZE];
static bool s_aes_write_pending;
static bool s_aes_copy_key;
static bool s_has_slix_password[SLIX_PASSWORD_COUNT];
static uint8_t s_slix_password[SLIX_PASSWORD_COUNT][SLIX_PASSWORD_BYTES];
static char s_slix_last_path[UL_AES_PATH_SIZE];
static char s_slix_write_path[UL_AES_PATH_SIZE];
static bool s_slix_write_pending;
static uint8_t s_st25tb_uid[ST25TB_WRITE_UID_SIZE];
static uint16_t s_st25tb_blocks;
static bool s_has_st25tb;
static bool s_st25tb_write_pending;
static uint8_t s_st25tb_write_block;
static uint8_t s_st25tb_write_data[ST25TB_WRITE_DATA_SIZE];

EXT_RAM_BSS_ATTR static nfc_t4t_card_t s_t4t_card;
static bool s_has_t4t;
static char s_t4t_path[NFC_ARCHIVE_PATH_SIZE];
static nfc_ntag4xx_card_t s_ntag4xx_card;
static bool s_has_ntag4xx;
static char s_ntag4xx_path[NFC_ARCHIVE_PATH_SIZE];

static nfc_desfire_card_t s_desfire_card;
static bool s_has_desfire;
static char s_desfire_path[NFC_ARCHIVE_PATH_SIZE];
static nfc_felica_card_t s_felica_card;
static bool s_has_felica;
static char s_felica_path[NFC_ARCHIVE_PATH_SIZE];
static uint8_t s_felica_key[NFC_FELICA_BYTES];
static bool s_has_felica_key;

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

static esp_err_t save_aes_card(const nfc_t2t_file_t *card) {
  if ((mkdir("/sdcard/nfc", S_IRWXU) != 0 && errno != EEXIST) ||
      (mkdir(UL_AES_DUMP_DIR, S_IRWXU) != 0 && errno != EEXIST))
    return ESP_FAIL;
  char uid[sizeof(card->config.uid) * HEX_DIGITS_PER_BYTE + 1];
  format_uid(card->config.uid, card->config.uid_len, false, uid, sizeof(uid));
  char path[UL_AES_PATH_SIZE];
  int fd = -1;
  for (unsigned i = 0; i < UL_AES_COPY_LIMIT; i++) {
    int length = snprintf(path, sizeof(path), UL_AES_DUMP_DIR "/aes_%s_%u.nfc", uid, i);
    if (length < 0 || (size_t)length >= sizeof(path))
      return ESP_ERR_INVALID_SIZE;
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd >= 0 || errno != EEXIST)
      break;
  }
  if (fd < 0)
    return ESP_FAIL;
  esp_err_t error = close(fd) == 0 ? nfc_t2t_file_save(path, card) : ESP_FAIL;
  if (error != ESP_OK)
    unlink(path);
  else {
    memcpy(s_aes_last_path, path, strlen(path) + 1);
    ESP_LOGI(TAG, "AES read saved: %s", path);
  }
  return error;
}

static hb_nfc_err_t read_plus(nfc_protocol_result_t *result) {
  nfc_iso14443a_data_t identity = {0};
  hb_nfc_err_t err = select_type_a(&identity);
  if (err != HB_NFC_OK)
    return err;
  uint8_t keys[PLUS_MAX_KEYS][MFP_BLOCK_SIZE];
  size_t count = 0;
  if (s_has_plus_key)
    memcpy(keys[count++], s_plus_key, MFP_BLOCK_SIZE);
  memset(keys[count++], 0, MFP_BLOCK_SIZE);
  memset(keys[count++], 0xFF, MFP_BLOCK_SIZE);
  nfc_dict_load_params_t dictionary = {.sd_path = "/sdcard/nfc/assets/dict/mf_plus.dic",
                                       .flash_path = "/assets/nfc/dict/mf_plus.dic",
                                       .key_size = MFP_BLOCK_SIZE,
                                       .max_keys = PLUS_MAX_KEYS - count};
  size_t loaded = 0;
  (void)nfc_dict_load_file(&dictionary, keys[count], &loaded);
  count += loaded;
  uint8_t security_level = UINT8_MAX;
  err = nfc_plus_poller_read(
      &identity, keys[0], count, cancel_requested, &s_plus_card, &security_level);
  memset(keys, 0, sizeof(keys));
  if (err != HB_NFC_OK)
    return err;
  unsigned blocks = 0, recovered = 0, admin = 0;
  for (unsigned i = 0; i < s_plus_card.block_count; i++)
    blocks += s_plus_card.has_block[i];
  for (unsigned i = 0; i < NFC_PLUS_FILE_MAX_KEYS; i++)
    recovered += s_plus_card.has_key[i];
  for (unsigned i = 0; i < 4U; i++)
    admin += s_plus_card.has_extra[i];
  snprintf(result->heading, sizeof(result->heading), "%s", s_plus_card.card_type);
  if (security_level != 3U || !s_plus_card.block_count) {
    if (security_level == UINT8_MAX)
      add_line(result, "Security level unknown");
    else
      add_line(result, "Security level SL%u", security_level);
    add_line(result, "Identity only; no memory operation");
    return HB_NFC_OK;
  }
  add_line(result, "SL3: %u/%u blocks", blocks, s_plus_card.block_count);
  add_line(result, "%u sector keys, %u admin keys", recovered, admin);
  add_line(result,
           "%s",
           blocks == s_plus_card.block_count ? "All blocks recovered"
                                             : "Partial read; unknown bytes preserved");
  if (cancel_requested())
    return ESP_ERR_INVALID_STATE;
  if ((mkdir("/sdcard/nfc", S_IRWXU) != 0 && errno != EEXIST) ||
      (mkdir(UL_AES_DUMP_DIR, S_IRWXU) != 0 && errno != EEXIST))
    return ESP_FAIL;
  char path[PLUS_PATH_SIZE];
  int fd = -1;
  for (unsigned i = 0; i < UL_AES_COPY_LIMIT; i++) {
    snprintf(path, sizeof(path), UL_AES_DUMP_DIR "/plus_%u.nfc", i);
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd >= 0 || errno != EEXIST)
      break;
  }
  if (fd < 0)
    return ESP_FAIL;
  err = close(fd) == 0 ? nfc_plus_file_save(path, &s_plus_card) : ESP_FAIL;
  if (err != ESP_OK) {
    unlink(path);
    return err;
  }
  memcpy(s_plus_last_path, path, strlen(path) + 1);
  add_line(result, "Saved: %s", strrchr(path, '/') + 1);
  return HB_NFC_OK;
}

static hb_nfc_err_t write_plus(nfc_protocol_result_t *result) {
  if (!s_plus_write_pending)
    return ESP_ERR_INVALID_STATE;
  s_plus_write_pending = false;
  hb_nfc_err_t err = nfc_plus_file_load(s_plus_write_path, &s_plus_card);
  if (err != ESP_OK)
    return err;
  nfc_iso14443a_data_t identity = {0};
  err = select_type_a(&identity);
  if (err != HB_NFC_OK)
    return err;
  uint16_t written = 0;
  err = nfc_plus_poller_write(
      &identity, &s_plus_card, s_plus_write_encrypted, cancel_requested, &written);
  snprintf(result->heading, sizeof(result->heading), "MIFARE Plus write");
  add_line(result, "%u ordinary blocks written", written);
  add_line(result, "UID, trailers and keys unchanged");
  if (err != HB_NFC_OK)
    add_line(result, "Stopped; read card to verify");
  return err;
}

static esp_err_t
capture_aes(nfc_iso14443a_data_t *identity, const uint8_t *version, nfc_t2t_file_t *capture) {
  uint8_t keys[PLUS_MAX_KEYS][T2T_AES_BLOCK_SIZE];
  size_t count = 0;
  if (s_has_ul_aes_key)
    memcpy(keys[count++], s_ul_aes_key, T2T_AES_BLOCK_SIZE);
  else {
    memset(keys[count++], 0, T2T_AES_BLOCK_SIZE);
    memset(keys[count++], UINT8_MAX, T2T_AES_BLOCK_SIZE);
    nfc_dict_load_params_t params = {.sd_path = "/sdcard/nfc/assets/dict/mf_ul_aes.dic",
                                     .flash_path = "/assets/nfc/dict/mf_ul_aes.dic",
                                     .key_size = T2T_AES_BLOCK_SIZE,
                                     .max_keys = PLUS_MAX_KEYS - count};
    size_t loaded = 0;
    (void)nfc_dict_load_file(&params, keys[count], &loaded);
    count += loaded;
  }
  esp_err_t error = ESP_ERR_INVALID_RESPONSE;
  for (size_t candidate = 0; candidate < count; candidate++) {
    for (unsigned mode = 0; mode < (s_has_ul_aes_key ? 1U : 2U); mode++) {
      if (cancel_requested()) {
        error = ESP_ERR_INVALID_STATE;
        goto cleanup;
      }
      error = iso14443a_poller_reselect(identity);
      if (error != ESP_OK)
        goto cleanup;
      t2t_aes_session_t session = {0};
      error = mful_aes_auth(
          &session, keys[candidate], 0, s_has_ul_aes_key ? s_ul_aes_secure : mode != 0);
      if (error != ESP_OK) {
        t2t_aes_reset(&session);
        break;
      }
      error = mful_aes_read_card(&session, version, capture);
      if (error == ESP_OK)
        goto cleanup;
    }
  }
cleanup:
  memset(keys, 0, sizeof(keys));
  return error;
}

static hb_nfc_err_t write_ul_aes(nfc_protocol_result_t *result) {
  if (!s_aes_write_pending)
    return ESP_ERR_INVALID_STATE;
  s_aes_write_pending = false;
  nfc_t2t_file_t *source = calloc(1, sizeof(*source));
  nfc_t2t_file_t *target = NULL;
  t2t_aes_session_t session = {0};
  uint8_t key[T2T_AES_BLOCK_SIZE] = {0};
  unsigned written = 0;
  esp_err_t error = ESP_ERR_NO_MEM;
  if (!source) {
    ESP_LOGE(TAG, "No memory for AES write image");
    goto cleanup;
  }
  error = nfc_t2t_file_load(s_aes_write_path, source);
  if (error != ESP_OK)
    goto cleanup;
  error = ESP_ERR_INVALID_ARG;
  if (source->config.type != T2T_TAG_ULTRALIGHT_AES || source->length != 60U * UL_AES_PAGE_SIZE ||
      (s_aes_copy_key && source->config.aes_key_known[0] != UINT16_MAX))
    goto cleanup;
  target = calloc(1, sizeof(*target));
  if (!target) {
    ESP_LOGE(TAG, "No memory for AES target");
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  nfc_iso14443a_data_t identity = {0};
  error = select_type_a(&identity);
  if (error != ESP_OK)
    goto cleanup;
  uint8_t version[UL_VERSION_SIZE];
  if (identity.sak != 0 || mful_get_version(version) != UL_VERSION_SIZE || !mful_is_aes(version)) {
    error = ESP_ERR_INVALID_RESPONSE;
    goto cleanup;
  }
  error = capture_aes(&identity, version, target);
  if (error != ESP_OK)
    goto cleanup;
  for (size_t i = 0; i < sizeof(key); i++)
    key[i] = target->memory[48U * UL_AES_PAGE_SIZE + sizeof(key) - 1 - i];
  error = iso14443a_poller_reselect(&identity);
  if (error != ESP_OK)
    goto cleanup;
  error = mful_aes_auth(&session, key, 0, (target->memory[41U * UL_AES_PAGE_SIZE] & 2U) != 0);
  if (error != ESP_OK)
    goto cleanup;
  for (unsigned page = 4; page < (s_aes_copy_key ? 52U : 40U); page++) {
    if (page >= 40U && page < 48U)
      continue;
    if (cancel_requested()) {
      error = ESP_ERR_INVALID_STATE;
      goto cleanup;
    }
    error = mful_aes_write_page(&session, page, source->memory + page * UL_AES_PAGE_SIZE);
    if (error != ESP_OK)
      goto cleanup;
    written++;
  }
cleanup:
  snprintf(result->heading, sizeof(result->heading), "Ultralight AES write");
  add_line(result, "%u pages acknowledged", written);
  add_line(result, s_aes_copy_key ? "DataProtKey copy requested" : "Target keys preserved");
  add_line(result, "UID, locks and configuration unchanged");
  if (error != ESP_OK)
    add_line(result, "Stopped; rescan target before retry");
  t2t_aes_reset(&session);
  memset(key, 0, sizeof(key));
  if (source)
    memset(source, 0, sizeof(*source));
  if (target)
    memset(target, 0, sizeof(*target));
  free(source);
  free(target);
  return error;
}

static hb_nfc_err_t save_capture(const char *device,
                                 const uint8_t *uid,
                                 size_t uid_length,
                                 const nfc_archive_field_t *fields,
                                 size_t count,
                                 nfc_protocol_result_t *result) {
  if (cancel_requested())
    return ESP_ERR_INVALID_STATE;
  char path[NFC_ARCHIVE_PATH_SIZE];
  esp_err_t error = nfc_archive_capture(device, uid, uid_length, fields, count, path, sizeof(path));
  if (result->line_count > NFC_PROTOCOL_RESULT_LINES - 2)
    result->line_count = NFC_PROTOCOL_RESULT_LINES - 2;
  add_line(result, error == ESP_OK ? "Capture saved to NFC dumps" : "Capture save failed");
  if (error == ESP_OK)
    add_line(result, "%s", strrchr(path, '/') + 1);
  return error;
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
  uint16_t limit = version_len == UL_VERSION_SIZE ? mful_version_pages(version) : 0;
  if (!limit)
    limit = UL_LEGACY_PAGES;
  err = iso14443a_poller_reselect(&card);
  if (err != HB_NFC_OK)
    return err;
  uint16_t i2c_pages = version_len == UL_VERSION_SIZE ? mful_i2c_pages(version) : 0;
  int pages = 0;
  if (s_has_ul_aes_key || (version_len == UL_VERSION_SIZE && mful_is_aes(version))) {
    if (version_len != UL_VERSION_SIZE || !mful_is_aes(version)) {
      add_line(result, "AES key set; tag is not Ultralight AES");
      return HB_NFC_ERR_PROTOCOL;
    }
    nfc_t2t_file_t *capture = malloc(sizeof(*capture));
    if (!capture) {
      ESP_LOGE(TAG, "Cannot allocate AES capture");
      err = ESP_ERR_NO_MEM;
      goto aes_capture_cleanup;
    }
    err = capture_aes(&card, version, capture);
    if (err == ESP_OK) {
      pages = capture->length / UL_AES_PAGE_SIZE;
      memcpy(memory, capture->memory, capture->length);
      esp_err_t save_error = save_aes_card(capture);
      err = save_error;
      add_line(result,
               save_error == ESP_OK ? "Saved AES copy to NFC dumps" : "AES read OK; save failed");
      add_line(result,
               "Signature %s; counters %02X",
               capture->config.has_aes_signature ? "captured" : "unknown",
               capture->config.counter_known);
    } else {
      add_line(result, "Incomplete AES read; no file saved");
    }
aes_capture_cleanup:
    if (capture)
      memset(capture, 0, sizeof(*capture));
    free(capture);
    if (err != ESP_OK)
      return err;
    add_line(result, "AES authenticated; key saved in dump");
  } else {
    pages =
        i2c_pages ? mful_read_i2c(version, memory, sizeof(memory)) : mful_read_all(memory, limit);
    if (pages > 0) {
      nfc_archive_field_t fields[] = {
          {"ATQA", card.atqa, sizeof(card.atqa)},
          {"SAK", &card.sak, 1},
          {"Readable pages", memory, (size_t)pages * UL_AES_PAGE_SIZE},
          {"Version response", version, version_len == UL_VERSION_SIZE ? sizeof(version) : 0}};
      size_t field_count = sizeof(fields) / sizeof(fields[0]);
      if (version_len != UL_VERSION_SIZE)
        field_count--;
      err = save_capture("Mifare Ultralight", card.uid, card.uid_len, fields, field_count, result);
      if (err != ESP_OK)
        return err;
    }
  }
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

#define CAPTURE_CC_BYTES  12U
#define CAPTURE_BYTE_BITS 8U

static void encode_cc(const t4t_cc_t *cc, uint8_t out[CAPTURE_CC_BYTES]) {
  const uint16_t words[] = {cc->cc_len, cc->mle, cc->mlc, cc->ndef_fid, cc->ndef_max};
  for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); i++) {
    out[i * 2] = (uint8_t)(words[i] >> CAPTURE_BYTE_BITS);
    out[i * 2 + 1] = (uint8_t)words[i];
  }
  out[CAPTURE_CC_BYTES - 2] = cc->read_access;
  out[CAPTURE_CC_BYTES - 1] = cc->write_access;
}

static esp_err_t save_t4t(nfc_protocol_result_t *result);

static hb_nfc_err_t read_ndef(nfc_protocol_result_t *result) {
  s_has_t4t = false;
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
  nfc_iso_dep_data_t dep = {0};
  uint8_t cc_bytes[CAPTURE_CC_BYTES] = {0};
  size_t cc_length = 0;
  if ((card.sak & 0x20U) != 0U) {
    err = iso_dep_rats(8, 0, &dep);
    if (err != HB_NFC_OK)
      return err;
    t4t_cc_t cc = {0};
    err = t4t_read_cc(&dep, &cc);
    if (err != HB_NFC_OK)
      return err;
    if (cc.nlen_size != 2)
      return ESP_ERR_NOT_SUPPORTED;
    nfc_t4t_defaults(&s_t4t_card);
    if ((card.uid_len != 4 && card.uid_len != 7) || !dep.ats_len || dep.ats[0] != dep.ats_len ||
        cc.ndef_max > NFC_T4T_FILE_SIZE || dep.ats_len > sizeof(s_t4t_card.identity.ats))
      return ESP_ERR_INVALID_SIZE;
    s_t4t_card.cc = cc;
    memcpy(s_t4t_card.identity.uid, card.uid, card.uid_len);
    s_t4t_card.identity.uid_len = card.uid_len;
    memcpy(s_t4t_card.identity.atqa, card.atqa, sizeof(card.atqa));
    s_t4t_card.identity.sak = card.sak;
    memcpy(s_t4t_card.identity.ats, dep.ats, dep.ats_len);
    s_t4t_card.identity.ats_len = dep.ats_len;
    err = t4t_read_ndef(&dep, &cc, s_t4t_card.file + 2, sizeof(s_t4t_card.file) - 2, &ndef_len);
    if (err != HB_NFC_OK || cancel_requested())
      return err != HB_NFC_OK ? err : ESP_ERR_INVALID_STATE;
    s_t4t_card.file[0] = ndef_len >> 8;
    s_t4t_card.file[1] = ndef_len;
    s_has_t4t = true;
    add_line(result,
             "Type 4: %u/%u bytes; write %s",
             (unsigned)ndef_len,
             cc.ndef_max - 2,
             cc.write_access == 0 ? "open" : "locked");
    format_ndef_records(s_t4t_card.file + 2, ndef_len, result);
    return save_t4t(result);
  } else if (card.sak == 0x00) {
    uint8_t memory[UL_MAX_PAGES * 4] = {0};
    uint8_t version[UL_VERSION_SIZE];
    int version_size = mful_get_version(version);
    uint16_t limit = version_size == UL_VERSION_SIZE ? mful_version_pages(version) : 0;
    if (!limit)
      limit = UL_LEGACY_PAGES;
    err = iso14443a_poller_reselect(&card);
    if (err != HB_NFC_OK)
      return err;
    const int pages = mful_read_all(memory, limit);
    if (pages <= 4)
      return HB_NFC_ERR_PROTOCOL;
    memcpy(cc_bytes, memory + 12, 4);
    cc_length = 4;
    ndef_tlv_t tlv;
    if (ndef_tlv_find_ndef(&memory[16], (size_t)pages * 4U - 16U, &tlv) != NDEF_TLV_STATUS_OK)
      return HB_NFC_ERR_NOT_FOUND;
    if (tlv.length > sizeof(ndef))
      return ESP_ERR_INVALID_SIZE;
    ndef_len = tlv.length;
    memcpy(ndef, tlv.value, ndef_len);
    add_line(result, "Type 2 Tag  %u bytes", (unsigned)tlv.length);
  } else {
    return HB_NFC_ERR_PROTOCOL;
  }
  format_ndef_records(ndef, ndef_len, result);
  nfc_archive_field_t fields[] = {{"ATQA", card.atqa, sizeof(card.atqa)},
                                  {"SAK", &card.sak, 1},
                                  {"NDEF", ndef, ndef_len},
                                  {"CC parsed", cc_bytes, cc_length},
                                  {"ATS", dep.ats, dep.ats_len}};
  size_t count = sizeof(fields) / sizeof(fields[0]);
  if (!dep.ats_len)
    count--;
  return save_capture("NDEF", card.uid, card.uid_len, fields, count, result);
}

static esp_err_t save_desfire(nfc_protocol_result_t *result) {
  if (mkdir("/sdcard/nfc", 0775) && errno != EEXIST)
    return ESP_FAIL;
  if (mkdir(NFC_ARCHIVE_DIRECTORY, 0775) && errno != EEXIST)
    return ESP_FAIL;
  for (unsigned index = 0; index < DESFIRE_FILE_LIMIT; index++) {
    char path[NFC_ARCHIVE_PATH_SIZE];
    snprintf(path, sizeof(path), NFC_ARCHIVE_DIRECTORY "/desfire_%04u.nfc", index);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0664);
    if (fd < 0) {
      if (errno == EEXIST)
        continue;
      return ESP_FAIL;
    }
    close(fd);
    esp_err_t error = nfc_desfire_save(path, &s_desfire_card);
    if (error != ESP_OK)
      unlink(path);
    else
      add_line(result, "Saved: %s", strrchr(path, '/') + 1);
    return error;
  }
  return ESP_ERR_NO_MEM;
}

static hb_nfc_err_t read_desfire(nfc_protocol_result_t *result) {
  nfc_desfire_free(&s_desfire_card);
  s_has_desfire = false;
  nfc_iso14443a_data_t card = {0};
  hb_nfc_err_t error = select_type_a(&card);
  if (error != HB_NFC_OK)
    return error;
  error = nfc_desfire_read(&card, &s_desfire_card, cancel_requested);
  if (error != ESP_OK)
    return error;
  if (cancel_requested()) {
    nfc_desfire_free(&s_desfire_card);
    return ESP_ERR_INVALID_STATE;
  }
  s_has_desfire = true;
  strlcpy(result->heading, "DESFire read", sizeof(result->heading));
  add_line(result, "%u applications; UP/DOWN details", (unsigned)s_desfire_card.app_count);
  add_line(result, "LEFT emulate identity only");
  error = save_desfire(result);
  if (error != ESP_OK)
    add_line(result, "Read in RAM; SD save failed");
  return error;
}

static esp_err_t load_desfire(nfc_protocol_result_t *result) {
  nfc_desfire_free(&s_desfire_card);
  s_has_desfire = false;
  esp_err_t error = nfc_desfire_load(s_desfire_path, &s_desfire_card);
  if (error != ESP_OK)
    return error;
  if (cancel_requested()) {
    nfc_desfire_free(&s_desfire_card);
    return ESP_ERR_INVALID_STATE;
  }
  s_has_desfire = true;
  strlcpy(result->heading, "DESFire file loaded", sizeof(result->heading));
  add_line(result, "%u applications; UP/DOWN details", (unsigned)s_desfire_card.app_count);
  add_line(result, "LEFT emulate identity only");
  error = save_desfire(result);
  if (error != ESP_OK)
    add_line(result, "Loaded in RAM; SD copy failed");
  return error;
}

static esp_err_t emulate_desfire(void) {
  if (!s_has_desfire)
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = rf_frontend_reserve(RF_FRONTEND_NFC_13MHZ);
  if (error != ESP_OK)
    return error;
  error = highboy_nfc_field_off();
  if (error == ESP_OK)
    error = t4t_emu_init_identity(&s_desfire_card.identity);
  if (error == ESP_OK)
    error = t4t_emu_configure_target();
  if (error == ESP_OK)
    error = t4t_emu_start();
  while (error == ESP_OK && !cancel_requested()) {
    t4t_emu_run_step();
    if (t4t_emu_has_error())
      error = ESP_FAIL;
    taskYIELD();
  }
  t4t_emu_stop();
  highboy_nfc_deinit();
  while (highboy_nfc_is_initialized()) {
    vTaskDelay(pdMS_TO_TICKS(DESFIRE_RETRY_MS));
    highboy_nfc_deinit();
  }
  esp_err_t release = rf_frontend_unreserve(RF_FRONTEND_NFC_13MHZ);
  return error == ESP_OK ? release : error;
}

hb_nfc_err_t nfc_protocol_service_desfire_file(const char *filename) {
  if (!filename || !*filename || strchr(filename, '/') || strchr(filename, '\\') ||
      strstr(filename, "..") || strpbrk(filename, "\r\n"))
    return HB_NFC_ERR_PARAM;
  char path[NFC_ARCHIVE_PATH_SIZE];
  int length = snprintf(path, sizeof(path), NFC_ARCHIVE_DIRECTORY "/%s", filename);
  if (length < 0 || (size_t)length >= sizeof(path))
    return HB_NFC_ERR_PARAM;
  portENTER_CRITICAL(&s_mux);
  esp_err_t error = s_status.running ? ESP_ERR_INVALID_STATE : ESP_OK;
  if (error == ESP_OK)
    memcpy(s_desfire_path, path, (size_t)length + 1);
  portEXIT_CRITICAL(&s_mux);
  return error;
}

esp_err_t
nfc_protocol_service_desfire_page(size_t page, char *out, size_t capacity, size_t *out_pages) {
  if (!out || !capacity || !out_pages)
    return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&s_mux);
  esp_err_t error = s_status.running || !s_has_desfire ? ESP_ERR_INVALID_STATE : ESP_OK;
  if (error == ESP_OK) {
    *out_pages = nfc_desfire_page_count(&s_desfire_card);
    nfc_desfire_page(&s_desfire_card, page % *out_pages, out, capacity);
  }
  portEXIT_CRITICAL(&s_mux);
  return error;
}

void nfc_protocol_service_desfire_clear(void) {
  nfc_protocol_service_stop();
  nfc_desfire_free(&s_desfire_card);
  s_has_desfire = false;
}

static esp_err_t save_t4t(nfc_protocol_result_t *result) {
  if (mkdir("/sdcard/nfc", 0775) && errno != EEXIST)
    return ESP_FAIL;
  if (mkdir(NFC_ARCHIVE_DIRECTORY, 0775) && errno != EEXIST)
    return ESP_FAIL;
  for (unsigned index = 0; index < DESFIRE_FILE_LIMIT; index++) {
    char path[NFC_ARCHIVE_PATH_SIZE];
    snprintf(path, sizeof(path), NFC_ARCHIVE_DIRECTORY "/t4t_%04u.nfc", index);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0664);
    if (fd < 0) {
      if (errno == EEXIST)
        continue;
      return ESP_FAIL;
    }
    close(fd);
    esp_err_t error = nfc_t4t_save(path, &s_t4t_card);
    if (error != ESP_OK)
      unlink(path);
    else
      add_line(result, "Saved: %s", strrchr(path, '/') + 1);
    return error;
  }
  return ESP_ERR_NO_MEM;
}

static esp_err_t load_t4t(nfc_protocol_result_t *result) {
  memset(&s_t4t_card, 0, sizeof(s_t4t_card));
  s_has_t4t = false;
  esp_err_t error = nfc_t4t_load(s_t4t_path, &s_t4t_card);
  if (error != ESP_OK)
    return error;
  if (cancel_requested()) {
    memset(&s_t4t_card, 0, sizeof(s_t4t_card));
    return ESP_ERR_INVALID_STATE;
  }
  s_has_t4t = true;
  strlcpy(result->heading, "Type 4 file loaded", sizeof(result->heading));
  add_line(result, "NDEF: %u bytes", (unsigned)nfc_t4t_length(&s_t4t_card));
  add_line(result, "LEFT emulate NDEF");
  error = save_t4t(result);
  if (error != ESP_OK)
    add_line(result, "Loaded in RAM; SD copy failed");
  return error;
}

static esp_err_t emulate_t4t(nfc_protocol_result_t *result) {
  if (!s_has_t4t)
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = rf_frontend_reserve(RF_FRONTEND_NFC_13MHZ);
  if (error != ESP_OK)
    return error;
  static nfc_t4t_listener_t listener;
  listener = (nfc_t4t_listener_t){.card = &s_t4t_card};
  error = highboy_nfc_field_off();
  if (error == ESP_OK)
    error = t4t_emu_init_ndef(&listener);
  if (error == ESP_OK)
    error = t4t_emu_configure_target();
  if (error == ESP_OK)
    error = t4t_emu_start();
  while (error == ESP_OK && !cancel_requested()) {
    t4t_emu_run_step();
    if (t4t_emu_has_error())
      error = ESP_FAIL;
    taskYIELD();
  }
  t4t_emu_stop();
  highboy_nfc_deinit();
  while (highboy_nfc_is_initialized()) {
    vTaskDelay(pdMS_TO_TICKS(DESFIRE_RETRY_MS));
    highboy_nfc_deinit();
  }
  esp_err_t release = rf_frontend_unreserve(RF_FRONTEND_NFC_13MHZ);
  if (listener.is_dirty) {
    esp_err_t saved = save_t4t(result);
    if (saved != ESP_OK) {
      add_line(result, "Changes in RAM; SD save failed");
      return saved;
    }
  }
  return error == ESP_OK ? release : error;
}

hb_nfc_err_t nfc_protocol_service_t4t_file(const char *filename) {
  if (!filename || !*filename || strchr(filename, '/') || strchr(filename, '\\') ||
      strstr(filename, "..") || strpbrk(filename, "\r\n"))
    return HB_NFC_ERR_PARAM;
  char path[NFC_ARCHIVE_PATH_SIZE];
  int length = snprintf(path, sizeof(path), NFC_ARCHIVE_DIRECTORY "/%s", filename);
  if (length < 0 || (size_t)length >= sizeof(path))
    return HB_NFC_ERR_PARAM;
  portENTER_CRITICAL(&s_mux);
  esp_err_t error = s_status.running ? ESP_ERR_INVALID_STATE : ESP_OK;
  if (error == ESP_OK)
    memcpy(s_t4t_path, path, (size_t)length + 1);
  portEXIT_CRITICAL(&s_mux);
  return error;
}

static esp_err_t write_t4t(nfc_protocol_result_t *result) {
  if (!s_has_t4t)
    return ESP_ERR_INVALID_STATE;
  nfc_iso14443a_data_t identity = {0};
  esp_err_t error = select_type_a(&identity);
  if (error != ESP_OK)
    return error;
  nfc_iso_dep_data_t dep = {0};
  error = iso_dep_rats(8, 0, &dep);
  if (error != ESP_OK)
    return error;
  t4t_cc_t cc;
  error = t4t_read_cc(&dep, &cc);
  if (error != ESP_OK)
    return error;
  if (cc.read_access != 0 || cc.nlen_size != 2)
    return ESP_ERR_NOT_SUPPORTED;
  if (cancel_requested())
    return ESP_ERR_INVALID_STATE;
  error = t4t_write_ndef(&dep, &cc, s_t4t_card.file + 2, nfc_t4t_length(&s_t4t_card));
  if (error != ESP_OK)
    return error;
  uint8_t actual[255];
  const size_t total = nfc_t4t_length(&s_t4t_card) + 2;
  for (size_t offset = 0; offset < total;) {
    if (cancel_requested())
      return ESP_ERR_INVALID_STATE;
    size_t chunk = total - offset;
    if (chunk > sizeof(actual))
      chunk = sizeof(actual);
    if (chunk > cc.mle)
      chunk = cc.mle;
    if (!chunk)
      return ESP_ERR_INVALID_RESPONSE;
    error = t4t_read_binary_ndef(&dep, &cc, offset, actual, chunk);
    if (error != ESP_OK)
      return error;
    if (memcmp(actual, s_t4t_card.file + offset, chunk))
      return ESP_ERR_INVALID_RESPONSE;
    offset += chunk;
  }
  strlcpy(result->heading, "Type 4 write verified", sizeof(result->heading));
  return ESP_OK;
}

static esp_err_t create_t4t(nfc_protocol_result_t *result) {
  nfc_iso14443a_data_t identity = {0};
  esp_err_t error = select_type_a(&identity);
  if (error == ESP_OK)
    error = nfc_t4t_create(&identity, cancel_requested);
  if (error != ESP_OK) {
    add_line(result, "Creation incomplete; existing apps are never erased");
    return error;
  }
  nfc_iso_dep_data_t dep = {0};
  error = iso14443a_poller_reselect(&identity);
  if (error == ESP_OK)
    error = iso_dep_rats(8, 0, &dep);
  if (error != ESP_OK)
    return error;
  t4t_cc_t cc;
  error = t4t_read_cc(&dep, &cc);
  if (error == ESP_OK)
    strlcpy(result->heading, "Type 4 application created", sizeof(result->heading));
  return error;
}

static esp_err_t save_ntag4xx(nfc_protocol_result_t *result) {
  if (mkdir("/sdcard/nfc", 0775) && errno != EEXIST)
    return ESP_FAIL;
  if (mkdir(NFC_ARCHIVE_DIRECTORY, 0775) && errno != EEXIST)
    return ESP_FAIL;
  for (unsigned index = 0; index < DESFIRE_FILE_LIMIT; index++) {
    char path[NFC_ARCHIVE_PATH_SIZE];
    snprintf(path, sizeof(path), NFC_ARCHIVE_DIRECTORY "/ntag4xx_%04u.nfc", index);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0664);
    if (fd < 0) {
      if (errno == EEXIST)
        continue;
      return ESP_FAIL;
    }
    close(fd);
    esp_err_t error = nfc_ntag4xx_save(path, &s_ntag4xx_card);
    if (error != ESP_OK)
      unlink(path);
    else
      add_line(result, "Saved: %s", strrchr(path, '/') + 1);
    return error;
  }
  return ESP_ERR_NO_MEM;
}

static esp_err_t load_ntag4xx(nfc_protocol_result_t *result) {
  memset(&s_ntag4xx_card, 0, sizeof(s_ntag4xx_card));
  s_has_ntag4xx = false;
  esp_err_t error = nfc_ntag4xx_load(s_ntag4xx_path, &s_ntag4xx_card);
  if (error != ESP_OK)
    return error;
  if (cancel_requested()) {
    memset(&s_ntag4xx_card, 0, sizeof(s_ntag4xx_card));
    return ESP_ERR_INVALID_STATE;
  }
  s_has_ntag4xx = true;
  strlcpy(result->heading, "NTAG4xx file loaded", sizeof(result->heading));
  add_line(result, "%s; UP/DOWN details", nfc_ntag4xx_name(&s_ntag4xx_card));
  add_line(result, "LEFT emulate identity only");
  error = save_ntag4xx(result);
  if (error != ESP_OK)
    add_line(result, "Loaded in RAM; SD copy failed");
  return error;
}

static esp_err_t emulate_ntag4xx(void) {
  if (!s_has_ntag4xx)
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = rf_frontend_reserve(RF_FRONTEND_NFC_13MHZ);
  if (error != ESP_OK)
    return error;
  error = highboy_nfc_field_off();
  if (error == ESP_OK)
    error = t4t_emu_init_identity(&s_ntag4xx_card.identity);
  if (error == ESP_OK)
    error = t4t_emu_configure_target();
  if (error == ESP_OK)
    error = t4t_emu_start();
  while (error == ESP_OK && !cancel_requested()) {
    t4t_emu_run_step();
    if (t4t_emu_has_error())
      error = ESP_FAIL;
    taskYIELD();
  }
  t4t_emu_stop();
  highboy_nfc_deinit();
  while (highboy_nfc_is_initialized()) {
    vTaskDelay(pdMS_TO_TICKS(DESFIRE_RETRY_MS));
    highboy_nfc_deinit();
  }
  esp_err_t release = rf_frontend_unreserve(RF_FRONTEND_NFC_13MHZ);
  return error == ESP_OK ? release : error;
}

hb_nfc_err_t nfc_protocol_service_ntag4xx_file(const char *filename) {
  if (!filename || !*filename || strchr(filename, '/') || strchr(filename, '\\') ||
      strstr(filename, "..") || strpbrk(filename, "\r\n"))
    return HB_NFC_ERR_PARAM;
  char path[NFC_ARCHIVE_PATH_SIZE];
  int length = snprintf(path, sizeof(path), NFC_ARCHIVE_DIRECTORY "/%s", filename);
  if (length < 0 || (size_t)length >= sizeof(path))
    return HB_NFC_ERR_PARAM;
  portENTER_CRITICAL(&s_mux);
  esp_err_t error = s_status.running ? ESP_ERR_INVALID_STATE : ESP_OK;
  if (error == ESP_OK)
    memcpy(s_ntag4xx_path, path, (size_t)length + 1);
  portEXIT_CRITICAL(&s_mux);
  return error;
}

esp_err_t
nfc_protocol_service_ntag4xx_page(size_t page, char *out, size_t capacity, size_t *out_pages) {
  if (!out || !capacity || !out_pages)
    return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&s_mux);
  esp_err_t error = s_status.running || !s_has_ntag4xx ? ESP_ERR_INVALID_STATE : ESP_OK;
  if (error == ESP_OK) {
    *out_pages = NFC_NTAG4XX_PAGES;
    nfc_ntag4xx_page(&s_ntag4xx_card, page % *out_pages, out, capacity);
  }
  portEXIT_CRITICAL(&s_mux);
  return error;
}

void nfc_protocol_service_ntag4xx_clear(void) {
  nfc_protocol_service_stop();
  memset(&s_ntag4xx_card, 0, sizeof(s_ntag4xx_card));
  s_has_ntag4xx = false;
}

hb_nfc_err_t nfc_protocol_service_felica_key(const char *text) {
  uint8_t key[NFC_FELICA_BYTES] = {0};
  if (text && *text) {
    if (strlen(text) != sizeof(key) * 2U)
      return ESP_ERR_INVALID_ARG;
    for (size_t i = 0; i < sizeof(key) * 2U; i++) {
      char c = text[i];
      int nibble = c >= '0' && c <= '9'   ? c - '0'
                   : c >= 'a' && c <= 'f' ? c - 'a' + 10
                   : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                          : -1;
      if (nibble < 0)
        return ESP_ERR_INVALID_ARG;
      key[i / 2U] = (key[i / 2U] << 4U) | nibble;
    }
  }
  portENTER_CRITICAL(&s_mux);
  esp_err_t error = s_status.running ? ESP_ERR_INVALID_STATE : ESP_OK;
  if (error == ESP_OK) {
    memcpy(s_felica_key, key, sizeof(key));
    s_has_felica_key = text && *text;
  }
  portEXIT_CRITICAL(&s_mux);
  memset(key, 0, sizeof(key));
  return error;
}

static hb_nfc_err_t save_v_memory(const iso15693_tag_t *tag,
                                  hb_nfc_protocol_t protocol,
                                  nfc_protocol_result_t *result) {
  if (!tag->info_valid || tag->block_count == 0 || tag->block_count > ISO15693_MAX_BLOCKS ||
      tag->block_size == 0 || tag->block_size > ISO15693_MAX_BLOCK_SIZE)
    return HB_NFC_ERR_PROTOCOL;
  size_t memory_size = (size_t)tag->block_count * tag->block_size;
  if (memory_size > NFC_STORE_PAYLOAD_MAX - NFC_V_DUMP_HEADER_SIZE) {
    add_line(result, "Tag exceeds NFC library capacity");
    return ESP_ERR_INVALID_SIZE;
  }
  uint8_t payload[NFC_STORE_PAYLOAD_MAX] = {NFC_V_DUMP_VERSION,
                                            (uint8_t)tag->block_count,
                                            (uint8_t)(tag->block_count >> NFC_V_BYTE_BITS),
                                            tag->block_size,
                                            tag->dsfid,
                                            tag->afi,
                                            tag->ic_ref,
                                            0};
  for (uint16_t block = 0; block < tag->block_count; block++) {
    if (cancel_requested())
      return ESP_ERR_INVALID_STATE;
    size_t length = 0;
    hb_nfc_err_t err =
        iso15693_read_single_block(tag,
                                   (uint8_t)block,
                                   &payload[NFC_V_DUMP_HEADER_SIZE + block * tag->block_size],
                                   tag->block_size,
                                   &length);
    if (err != HB_NFC_OK || length != tag->block_size) {
      add_line(result, "Read failed at block %u", block);
      return err == HB_NFC_OK ? HB_NFC_ERR_PROTOCOL : err;
    }
  }
  char uid[32], name[NFC_STORE_NAME_MAX];
  format_uid(tag->uid, sizeof(tag->uid), true, uid, sizeof(uid));
  snprintf(name, sizeof(name), "%s %.16s", protocol == HB_PROTO_SLIX ? "SLIX" : "NFC-V", uid);
  nfc_store_card_t saved = {.name = name,
                            .protocol = protocol,
                            .uid = tag->uid,
                            .uid_len = sizeof(tag->uid),
                            .payload = payload,
                            .payload_len = NFC_V_DUMP_HEADER_SIZE + memory_size};
  nfc_store_init();
  hb_nfc_err_t err = nfc_store_save(&saved);
  add_line(result, err == HB_NFC_OK ? "All blocks saved to NFC library" : "Could not save dump");
  nfc_archive_field_t fields[] = {{"Geometry and identity", payload, NFC_V_DUMP_HEADER_SIZE},
                                  {"Memory", payload + NFC_V_DUMP_HEADER_SIZE, memory_size}};
  hb_nfc_err_t archive_error = save_capture(
      "ISO15693-3", tag->uid, sizeof(tag->uid), fields, sizeof(fields) / sizeof(fields[0]), result);
  return archive_error == HB_NFC_OK ? err : archive_error;
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
  return save_v_memory(&tag, HB_PROTO_ISO15693, result);
}

static esp_err_t save_felica(nfc_protocol_result_t *result) {
  if (mkdir("/sdcard/nfc", 0775) && errno != EEXIST)
    return ESP_FAIL;
  if (mkdir(NFC_ARCHIVE_DIRECTORY, 0775) && errno != EEXIST)
    return ESP_FAIL;
  for (unsigned index = 0; index < DESFIRE_FILE_LIMIT; index++) {
    char path[NFC_ARCHIVE_PATH_SIZE];
    snprintf(path, sizeof(path), NFC_ARCHIVE_DIRECTORY "/felica_%04u.nfc", index);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0664);
    if (fd < 0) {
      if (errno == EEXIST)
        continue;
      return ESP_FAIL;
    }
    close(fd);
    esp_err_t error = nfc_felica_save(path, &s_felica_card);
    if (error != ESP_OK)
      unlink(path);
    else
      add_line(result, "Saved: %s", strrchr(path, '/') + 1);
    return error;
  }
  return ESP_ERR_NO_MEM;
}

static hb_nfc_err_t read_felica(nfc_protocol_result_t *result) {
  nfc_felica_free(&s_felica_card);
  s_has_felica = false;
  nfc_felica_read_options_t options = {.key = s_has_felica_key ? s_felica_key : NULL,
                                       .should_cancel = cancel_requested};
  esp_err_t error = nfc_felica_read(&s_felica_card, &options);
  if (error != ESP_OK)
    return error;
  if (cancel_requested()) {
    nfc_felica_free(&s_felica_card);
    return ESP_ERR_INVALID_STATE;
  }
  s_has_felica = true;
  strlcpy(result->heading, "FeliCa read", sizeof(result->heading));
  add_line(result, "%u systems; UP/DOWN details", (unsigned)s_felica_card.system_count);
  add_line(result, "LEFT emulate saved profile");
  error = save_felica(result);
  if (error != ESP_OK)
    add_line(result, "Read in RAM; SD save failed");
  return error;
}

static esp_err_t load_felica(nfc_protocol_result_t *result) {
  nfc_felica_free(&s_felica_card);
  s_has_felica = false;
  esp_err_t error = nfc_felica_load(s_felica_path, &s_felica_card);
  if (error != ESP_OK)
    return error;
  if (cancel_requested()) {
    nfc_felica_free(&s_felica_card);
    return ESP_ERR_INVALID_STATE;
  }
  s_has_felica = true;
  strlcpy(result->heading, "FeliCa file loaded", sizeof(result->heading));
  add_line(result, "%u systems; UP/DOWN details", (unsigned)s_felica_card.system_count);
  add_line(result, "LEFT emulate saved profile");
  error = save_felica(result);
  if (error != ESP_OK)
    add_line(result, "Loaded in RAM; SD copy failed");
  return error;
}

static esp_err_t emulate_felica(nfc_protocol_result_t *result) {
  if (!s_has_felica)
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = rf_frontend_reserve(RF_FRONTEND_NFC_13MHZ);
  if (error != ESP_OK)
    return error;
  error = highboy_nfc_field_off();
  if (error == ESP_OK)
    error = felica_emu_init_profile(&s_felica_card);
  const bool has_session = error == ESP_OK;
  if (error == ESP_OK)
    error = felica_emu_configure_target();
  if (error == ESP_OK)
    error = felica_emu_start();
  while (error == ESP_OK && !cancel_requested()) {
    felica_emu_run_step();
    error = felica_emu_error();
    taskYIELD();
  }
  felica_emu_stop();
  highboy_nfc_deinit();
  while (highboy_nfc_is_initialized()) {
    vTaskDelay(pdMS_TO_TICKS(DESFIRE_RETRY_MS));
    highboy_nfc_deinit();
  }
  esp_err_t release = rf_frontend_unreserve(RF_FRONTEND_NFC_13MHZ);
  if (has_session && felica_emu_dirty()) {
    esp_err_t saved = save_felica(result);
    if (saved != ESP_OK) {
      add_line(result, "Session in RAM; SD save failed");
      return saved;
    }
  }
  return error == ESP_OK ? release : error;
}

hb_nfc_err_t nfc_protocol_service_felica_file(const char *filename) {
  if (!filename || !*filename || strchr(filename, '/') || strchr(filename, '\\') ||
      strstr(filename, "..") || strpbrk(filename, "\r\n"))
    return HB_NFC_ERR_PARAM;
  char path[NFC_ARCHIVE_PATH_SIZE];
  int length = snprintf(path, sizeof(path), NFC_ARCHIVE_DIRECTORY "/%s", filename);
  if (length < 0 || (size_t)length >= sizeof(path))
    return HB_NFC_ERR_PARAM;
  portENTER_CRITICAL(&s_mux);
  esp_err_t error = s_status.running ? ESP_ERR_INVALID_STATE : ESP_OK;
  if (error == ESP_OK)
    memcpy(s_felica_path, path, (size_t)length + 1);
  portEXIT_CRITICAL(&s_mux);
  return error;
}

esp_err_t
nfc_protocol_service_felica_page(size_t page, char *out, size_t capacity, size_t *out_pages) {
  if (!out || !capacity || !out_pages)
    return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&s_mux);
  esp_err_t error = s_status.running || !s_has_felica ? ESP_ERR_INVALID_STATE : ESP_OK;
  if (error == ESP_OK) {
    *out_pages = nfc_felica_page_count(&s_felica_card);
    nfc_felica_page(&s_felica_card, page % *out_pages, out, capacity);
  }
  portEXIT_CRITICAL(&s_mux);
  return error;
}

void nfc_protocol_service_felica_clear(void) {
  nfc_protocol_service_stop();
  nfc_felica_free(&s_felica_card);
  s_has_felica = false;
  memset(s_felica_key, 0, sizeof(s_felica_key));
  s_has_felica_key = false;
}

#define ST25TB_CMD_INITIATE    0x06U
#define ST25TB_CMD_SELECT      0x0EU
#define ST25TB_CMD_UID         0x0BU
#define ST25TB_CMD_READ        0x08U
#define ST25TB_SYSTEM_BLOCK    0xFFU
#define ST25TB_MAX_BLOCKS      128U
#define ST25TB_PAYLOAD_HEADER  8U
#define ST25TB_PAYLOAD_VERSION 1U
#define ST25TB_UID_SIZE        8U
#define ST25TB_BLOCK_SIZE      4U
#define ST25TB_TIMEOUT_MS      20
#define NFC_FRAME_CRC_SIZE     2U
#define NFC_NXP_VENDOR         0x04U
#define NTAG4XX_PRODUCT_TYPE   0x04U
#define SLIX_UID_MARKER        0xE0U
#define SLIX_CMD_SIGNATURE     0xBDU
#define SLIX_SIGNATURE_SIZE    32U

static hb_nfc_err_t
st25tb_exchange(const uint8_t *cmd, size_t cmd_len, uint8_t *out, size_t expected) {
  uint8_t response[ST25TB_UID_SIZE + NFC_FRAME_CRC_SIZE];
  if (out == NULL || expected > ST25TB_UID_SIZE)
    return HB_NFC_ERR_PARAM;
  int len = nfc_poller_transceive(cmd,
                                  cmd_len,
                                  true,
                                  response,
                                  sizeof(response),
                                  expected + NFC_FRAME_CRC_SIZE,
                                  ST25TB_TIMEOUT_MS);
  if (len == 0)
    return HB_NFC_ERR_TIMEOUT;
  if ((size_t)len != expected + NFC_FRAME_CRC_SIZE || !iso15693_check_crc(response, (size_t)len))
    return HB_NFC_ERR_PROTOCOL;
  memcpy(out, response, expected);
  return HB_NFC_OK;
}

static hb_nfc_err_t read_st25tb(nfc_protocol_result_t *result) {
  s_has_st25tb = false;
  s_st25tb_write_pending = false;
  hb_nfc_err_t err = iso14443b_poller_init();
  if (err != HB_NFC_OK)
    return err;
  uint8_t cmd[2] = {ST25TB_CMD_INITIATE, 0};
  uint8_t chip_id;
  err = st25tb_exchange(cmd, sizeof(cmd), &chip_id, 1);
  if (err != HB_NFC_OK)
    return err;
  cmd[0] = ST25TB_CMD_SELECT;
  cmd[1] = chip_id;
  uint8_t selected;
  err = st25tb_exchange(cmd, sizeof(cmd), &selected, 1);
  if (err != HB_NFC_OK)
    return err;
  if (selected != chip_id)
    return HB_NFC_ERR_PROTOCOL;
  cmd[0] = ST25TB_CMD_UID;
  uint8_t uid[ST25TB_UID_SIZE];
  err = st25tb_exchange(cmd, 1, uid, sizeof(uid));
  if (err != HB_NFC_OK)
    return err;
  uint16_t blocks;
  switch (uid[5] >> 2) {
    case 0x00:
    case 0x03:
    case 0x07:
      blocks = 128;
      break;
    case 0x04:
    case 0x06:
    case 0x0C:
      blocks = 16;
      break;
    case 0x0F:
      blocks = 64;
      break;
    default:
      return HB_NFC_ERR_PROTOCOL;
  }
  char text[32];
  format_uid(uid, sizeof(uid), true, text, sizeof(text));
  strlcpy(result->heading, "ST25TB", sizeof(result->heading));
  add_line(result, "UID %s", text);
  add_line(result, "%u blocks x %u bytes", blocks, ST25TB_BLOCK_SIZE);
  uint8_t payload[ST25TB_PAYLOAD_HEADER + ST25TB_MAX_BLOCKS * ST25TB_BLOCK_SIZE] = {0};
  payload[0] = ST25TB_PAYLOAD_VERSION;
  payload[1] = (uint8_t)blocks;
  payload[2] = (uint8_t)(blocks >> 8);
  payload[3] = ST25TB_BLOCK_SIZE;
  for (uint16_t block = 0; block < blocks; block++) {
    if (cancel_requested())
      return ESP_ERR_INVALID_STATE;
    uint8_t data[ST25TB_BLOCK_SIZE];
    cmd[0] = ST25TB_CMD_READ;
    cmd[1] = (uint8_t)block;
    err = st25tb_exchange(cmd, sizeof(cmd), data, sizeof(data));
    if (err != HB_NFC_OK) {
      add_line(result, "Read %u/%u blocks", block, blocks);
      return err;
    }
    memcpy(&payload[ST25TB_PAYLOAD_HEADER + block * ST25TB_BLOCK_SIZE], data, sizeof(data));
    if (block < 3)
      add_line(result, "B%u %02X %02X %02X %02X", block, data[0], data[1], data[2], data[3]);
  }
  cmd[0] = ST25TB_CMD_READ;
  cmd[1] = ST25TB_SYSTEM_BLOCK;
  err = st25tb_exchange(cmd, sizeof(cmd), &payload[ST25TB_BLOCK_SIZE], ST25TB_BLOCK_SIZE);
  if (err != HB_NFC_OK)
    return err;
  char name[NFC_STORE_NAME_MAX];
  snprintf(name, sizeof(name), "ST25TB %.16s", text);
  nfc_store_card_t saved = {.name = name,
                            .protocol = HB_PROTO_ST25TB,
                            .uid = uid,
                            .uid_len = sizeof(uid),
                            .payload = payload,
                            .payload_len = ST25TB_PAYLOAD_HEADER + blocks * ST25TB_BLOCK_SIZE};
  nfc_store_init();
  err = nfc_store_save(&saved);
  memcpy(s_st25tb_uid, uid, sizeof(uid));
  s_st25tb_blocks = blocks;
  s_has_st25tb = true;
  add_line(result, err == HB_NFC_OK ? "Saved blocks + system OTP" : "Could not save dump");
  nfc_archive_field_t fields[] = {
      {"Geometry", payload, ST25TB_BLOCK_SIZE},
      {"System OTP Block", payload + ST25TB_BLOCK_SIZE, ST25TB_BLOCK_SIZE},
      {"Memory", payload + ST25TB_PAYLOAD_HEADER, (size_t)blocks * ST25TB_BLOCK_SIZE}};
  hb_nfc_err_t archive_error =
      save_capture("ST25TB", uid, sizeof(uid), fields, sizeof(fields) / sizeof(fields[0]), result);
  return archive_error == HB_NFC_OK ? err : archive_error;
}

static hb_nfc_err_t write_st25tb(nfc_protocol_result_t *result) {
  if (!s_has_st25tb || !s_st25tb_write_pending)
    return ESP_ERR_INVALID_STATE;
  s_st25tb_write_pending = false;
  if (s_st25tb_write_block >= s_st25tb_blocks)
    return HB_NFC_ERR_PARAM;
  hb_nfc_err_t err = iso14443b_poller_init();
  if (err != HB_NFC_OK)
    return err;
  uint8_t select[2] = {ST25TB_CMD_INITIATE, 0};
  uint8_t chip_id, selected;
  err = st25tb_exchange(select, sizeof(select), &chip_id, 1);
  if (err != HB_NFC_OK)
    return err;
  select[0] = ST25TB_CMD_SELECT;
  select[1] = chip_id;
  err = st25tb_exchange(select, sizeof(select), &selected, 1);
  if (err != HB_NFC_OK || selected != chip_id)
    return err != HB_NFC_OK ? err : HB_NFC_ERR_PROTOCOL;
  uint8_t uid_command = ST25TB_CMD_UID;
  uint8_t uid[ST25TB_UID_SIZE];
  err = st25tb_exchange(&uid_command, 1, uid, sizeof(uid));
  if (err != HB_NFC_OK)
    return err;
  if (memcmp(uid, s_st25tb_uid, sizeof(uid))) {
    add_line(result, "Tag differs from the last read");
    return HB_NFC_ERR_PROTOCOL;
  }
  if (cancel_requested())
    return ESP_ERR_INVALID_STATE;
  uint8_t command[2 + ST25TB_BLOCK_SIZE] = {ST25TB_CMD_WRITE, s_st25tb_write_block};
  memcpy(command + 2, s_st25tb_write_data, ST25TB_BLOCK_SIZE);
  uint8_t unexpected[ST25TB_BLOCK_SIZE + NFC_FRAME_CRC_SIZE];
  int received = nfc_poller_transceive(
      command, sizeof(command), true, unexpected, sizeof(unexpected), 0, ST25TB_TIMEOUT_MS);
  /* These tags do not ACK writes; timeout alone is not evidence of success. */
  if (received != 0)
    return HB_NFC_ERR_PROTOCOL;
  vTaskDelay(pdMS_TO_TICKS(ST25TB_PROGRAM_MS) + 1);
  uint8_t read[2] = {ST25TB_CMD_READ, s_st25tb_write_block};
  uint8_t actual[ST25TB_BLOCK_SIZE];
  err = st25tb_exchange(read, sizeof(read), actual, sizeof(actual));
  if (err != HB_NFC_OK || memcmp(actual, s_st25tb_write_data, sizeof(actual))) {
    add_line(result, "Write could not be verified");
    return err != HB_NFC_OK ? err : HB_NFC_ERR_PROTOCOL;
  }
  strlcpy(result->heading, "ST25TB write verified", sizeof(result->heading));
  add_line(result,
           "Block %02X: %02X %02X %02X %02X",
           s_st25tb_write_block,
           actual[0],
           actual[1],
           actual[2],
           actual[3]);
  add_line(result, "Rescan to save updated memory");
  return HB_NFC_OK;
}

static hb_nfc_err_t read_ntag4xx(nfc_protocol_result_t *result) {
  memset(&s_ntag4xx_card, 0, sizeof(s_ntag4xx_card));
  s_has_ntag4xx = false;
  nfc_iso14443a_data_t card = {0};
  hb_nfc_err_t error = select_type_a(&card);
  if (error != HB_NFC_OK)
    return error;
  mf_desfire_session_t session = {0};
  error = mf_desfire_poller_init(&card, &session);
  if (error != HB_NFC_OK)
    return error;
  session.should_cancel = cancel_requested;
  error = nfc_ntag4xx_read(&session, &card, &s_ntag4xx_card);
  if (error != ESP_OK)
    return error;
  strlcpy(result->heading, nfc_ntag4xx_name(&s_ntag4xx_card), sizeof(result->heading));
  add_line(result, "UP/DOWN details; LEFT identity replay");
  t4t_cc_t cc = {0};
  error = t4t_read_cc(&session.dep, &cc);
  if (error == HB_NFC_OK) {
    s_ntag4xx_card.has_cc = true;
    encode_cc(&cc, s_ntag4xx_card.cc);
    if (!cancel_requested()) {
      error = t4t_read_ndef(&session.dep,
                            &cc,
                            s_ntag4xx_card.ndef,
                            sizeof(s_ntag4xx_card.ndef),
                            &s_ntag4xx_card.ndef_length);
      s_ntag4xx_card.has_ndef = error == HB_NFC_OK;
      if (s_ntag4xx_card.has_ndef)
        format_ndef_records(s_ntag4xx_card.ndef, s_ntag4xx_card.ndef_length, result);
      else {
        s_ntag4xx_card.ndef_length = 0;
        add_line(result, "NDEF unavailable: %s", esp_err_to_name(error));
      }
    }
  } else
    add_line(result, "CC unavailable: %s", esp_err_to_name(error));
  if (cancel_requested()) {
    memset(&s_ntag4xx_card, 0, sizeof(s_ntag4xx_card));
    return ESP_ERR_INVALID_STATE;
  }
  s_has_ntag4xx = true;
  error = save_ntag4xx(result);
  if (error != ESP_OK)
    add_line(result, "Read in RAM; SD save failed");
  return error;
}

#define SLIX_PRODUCT_ID       1U
#define SLIX_PRODUCT_S        2U
#define SLIX_PRODUCT_L        3U
#define SLIX_GENERATION_SHIFT 3U
#define SLIX_GENERATION_MASK  3U
#define SLIX_LOCK_AFI         0x01U
#define SLIX_LOCK_EAS         0x02U
#define SLIX_LOCK_DSFID       0x04U
#define SLIX_LOCK_PROTECTION  0x08U
#define SLIX_COUNTER_BLOCK    79U

static bool is_slix2(const iso15693_tag_t *tag) {
  return tag->uid[5] == SLIX_PRODUCT_ID &&
         ((tag->uid[4] >> SLIX_GENERATION_SHIFT) & SLIX_GENERATION_MASK) == 1;
}

static hb_nfc_err_t select_slix(iso15693_tag_t *out_tag) {
  hb_nfc_err_t err = iso15693_poller_init();
  if (err != HB_NFC_OK)
    return err;
  if (s_has_slix_password[SLIX_PASSWORD_PRIVACY]) {
    err = slix_authenticate(NULL, SLIX_PASSWORD_PRIVACY, s_slix_password[SLIX_PASSWORD_PRIVACY]);
    if (err != HB_NFC_OK)
      return err;
  }
  err = iso15693_inventory(out_tag);
  if (err != HB_NFC_OK)
    return err;
  if (out_tag->uid[7] != SLIX_UID_MARKER || out_tag->uid[6] != NFC_NXP_VENDOR ||
      out_tag->uid[5] < SLIX_PRODUCT_ID || out_tag->uid[5] > SLIX_PRODUCT_L)
    return HB_NFC_ERR_PROTOCOL;
  err = iso15693_get_system_info(out_tag);
  if (err != HB_NFC_OK)
    return err;
  if (!out_tag->info_valid || !out_tag->block_count || out_tag->block_count > ISO15693_MAX_BLOCKS ||
      !out_tag->block_size || out_tag->block_size > ISO15693_MAX_BLOCK_SIZE)
    return HB_NFC_ERR_PROTOCOL;
  for (slix_password_t type = SLIX_PASSWORD_READ; type < SLIX_PASSWORD_COUNT; type++) {
    if (type == SLIX_PASSWORD_PRIVACY || !s_has_slix_password[type])
      continue;
    err = slix_authenticate(out_tag, type, s_slix_password[type]);
    if (err != HB_NFC_OK)
      return err;
  }
  return HB_NFC_OK;
}

static hb_nfc_err_t read_slix(nfc_protocol_result_t *result) {
  iso15693_tag_t tag = {0};
  hb_nfc_err_t err = select_slix(&tag);
  if (err != HB_NFC_OK)
    return err;
  strlcpy(result->heading, "ICODE SLIX", sizeof(result->heading));
  char uid[32];
  format_uid(tag.uid, sizeof(tag.uid), true, uid, sizeof(uid));
  add_line(result, "UID %s", uid);
  add_line(result, "%u blocks x %u bytes", tag.block_count, tag.block_size);
  s_has_v = false;
  s_v_result_valid = false;
  memset(&s_v_card, 0, sizeof(s_v_card));
  s_v_card.is_slix = true;
  s_v_card.is_read_only = true;
  memcpy(s_v_card.uid, tag.uid, sizeof(tag.uid));
  s_v_card.block_count = tag.block_count;
  s_v_card.block_size = tag.block_size;
  s_v_card.dsfid = tag.dsfid;
  s_v_card.afi = tag.afi;
  s_v_card.ic_ref = tag.ic_ref;
  memcpy(s_v_card.has_password, s_has_slix_password, sizeof(s_has_slix_password));
  memcpy(s_v_card.passwords, s_slix_password, sizeof(s_slix_password));
  if (is_slix2(&tag)) {
    slix_system_info_t info;
    err = slix_get_system_info(&tag, &info);
    if (err != HB_NFC_OK)
      return err;
    if (info.protection_pointer > tag.block_count)
      return HB_NFC_ERR_PROTOCOL;
    s_v_card.has_system_info = true;
    s_v_card.protection_pointer = info.protection_pointer;
    s_v_card.protection_condition = info.protection_condition;
    s_v_card.afi_locked = (info.lock_bits & SLIX_LOCK_AFI) != 0;
    s_v_card.dsfid_locked = (info.lock_bits & SLIX_LOCK_DSFID) != 0;
    s_v_card.eas_locked = (info.lock_bits & SLIX_LOCK_EAS) != 0;
    s_v_card.protection_locked = (info.lock_bits & SLIX_LOCK_PROTECTION) != 0;
    err = slix_read_signature(&tag, s_v_card.signature);
    if (err != HB_NFC_OK)
      return err;
    s_v_card.has_signature = true;
    add_line(result, "Signature saved (not verified)");
  }
  for (uint16_t block = 0; block < tag.block_count; block++) {
    if (cancel_requested())
      return ESP_ERR_INVALID_STATE;
    size_t length = 0;
    err = iso15693_read_single_block(
        &tag, (uint8_t)block, s_v_card.memory + block * tag.block_size, tag.block_size, &length);
    if (err != HB_NFC_OK || length != tag.block_size) {
      add_line(result, "Read failed at block %u", block);
      return err == HB_NFC_OK ? HB_NFC_ERR_PROTOCOL : err;
    }
    uint8_t security = 0;
    err = iso15693_get_multi_block_sec(&tag, (uint8_t)block, 1, &security, 1, &length);
    if (err != HB_NFC_OK || length != 1)
      return err == HB_NFC_OK ? HB_NFC_ERR_PROTOCOL : err;
    s_v_card.locked[block] = (security & 1U) != 0;
  }
  if (cancel_requested())
    return ESP_ERR_INVALID_STATE;
  if ((mkdir("/sdcard/nfc", S_IRWXU) != 0 && errno != EEXIST) ||
      (mkdir(UL_AES_DUMP_DIR, S_IRWXU) != 0 && errno != EEXIST))
    return ESP_FAIL;
  char path[UL_AES_PATH_SIZE];
  int fd = -1;
  for (unsigned i = 0; i < UL_AES_COPY_LIMIT; i++) {
    snprintf(path, sizeof(path), UL_AES_DUMP_DIR "/slix_%s_%u.nfc", uid, i);
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd >= 0 || errno != EEXIST)
      break;
  }
  if (fd < 0)
    return ESP_FAIL;
  err = close(fd) == 0 ? nfc_v_file_save(path, &s_v_card) : ESP_FAIL;
  if (err != ESP_OK) {
    unlink(path);
    return err;
  }
  memcpy(s_slix_last_path, path, strlen(path) + 1);
  s_has_v = true;
  add_line(result, "Saved: %s", strrchr(path, '/') + 1);
  return HB_NFC_OK;
}

static hb_nfc_err_t write_slix(nfc_protocol_result_t *result) {
  if (!s_slix_write_pending)
    return ESP_ERR_INVALID_STATE;
  s_slix_write_pending = false;
  s_has_v = false;
  s_v_result_valid = false;
  hb_nfc_err_t err = nfc_v_file_load(s_slix_write_path, &s_v_card);
  if (err != ESP_OK)
    return err;
  if (!s_v_card.is_slix)
    return HB_NFC_ERR_PARAM;
  iso15693_tag_t tag = {0};
  err = select_slix(&tag);
  if (err != HB_NFC_OK)
    return err;
  if (memcmp(tag.uid, s_v_card.uid, sizeof(tag.uid)) || tag.block_count != s_v_card.block_count ||
      tag.block_size != s_v_card.block_size)
    return HB_NFC_ERR_PARAM;
  uint16_t verified = 0;
  for (uint16_t block = 0; block < tag.block_count; block++) {
    if (is_slix2(&tag) && block == SLIX_COUNTER_BLOCK)
      continue;
    if (cancel_requested()) {
      err = ESP_ERR_INVALID_STATE;
      break;
    }
    uint8_t data[ISO15693_MAX_BLOCK_SIZE];
    size_t length = 0;
    err = iso15693_read_single_block(&tag, (uint8_t)block, data, sizeof(data), &length);
    if (err != HB_NFC_OK || length != tag.block_size) {
      if (err == HB_NFC_OK)
        err = HB_NFC_ERR_PROTOCOL;
      break;
    }
    const uint8_t *desired = s_v_card.memory + block * tag.block_size;
    if (memcmp(data, desired, length)) {
      uint8_t security = 0;
      err = iso15693_get_multi_block_sec(&tag, (uint8_t)block, 1, &security, 1, &length);
      if (err != HB_NFC_OK)
        break;
      if (length != 1 || (security & 1U)) {
        err = HB_NFC_ERR_PROTOCOL;
        break;
      }
      err = iso15693_write_single_block(&tag, (uint8_t)block, desired, tag.block_size);
      if (err != HB_NFC_OK)
        break;
      err = iso15693_read_single_block(&tag, (uint8_t)block, data, sizeof(data), &length);
      if (err != HB_NFC_OK)
        break;
      if (length != tag.block_size || memcmp(data, desired, length)) {
        err = HB_NFC_ERR_PROTOCOL;
        break;
      }
    }
    verified++;
  }
  add_line(result, "%u data blocks verified", verified);
  add_line(result, "Keys, locks, counter unchanged");
  return err;
}

static hb_nfc_err_t read_texkom(nfc_protocol_result_t *result) {
  texkom_data_t data = {0};
  esp_err_t err = ESP_ERR_TIMEOUT;
  for (size_t i = 0; i < TEXKOM_READ_ATTEMPTS; i++) {
    if (cancel_requested())
      return ESP_ERR_INVALID_STATE;
    err = texkom_read(&data);
    if (err == ESP_OK)
      break;
    if (err != ESP_ERR_TIMEOUT && err != ESP_ERR_INVALID_RESPONSE && err != ESP_ERR_INVALID_ARG &&
        err != ESP_ERR_INVALID_SIZE)
      return err;
    vTaskDelay(pdMS_TO_TICKS(TEXKOM_RETRY_DELAY_MS));
  }
  if (err != ESP_OK)
    return err;
  s_texkom_last = data;
  s_has_texkom = true;
  strlcpy(result->heading, "Texkom", sizeof(result->heading));
  add_line(result, "Type %s", texkom_type_name(data.type));
  char frame[NFC_PROTOCOL_RESULT_LINE_LEN];
  format_uid(data.frame, sizeof(data.frame), false, frame, sizeof(frame));
  add_line(result, "Frame %s", frame);
  add_line(result, "Checksum verified");
  char name[NFC_STORE_NAME_MAX];
  snprintf(name, sizeof(name), "Texkom %.16s", frame);
  nfc_store_card_t saved = {.name = name,
                            .protocol = HB_PROTO_TEXKOM,
                            .uid = &data.frame[data.type == TEXKOM_TYPE_MMBIT ? 4 : 3],
                            .uid_len = data.type == TEXKOM_TYPE_MMBIT ? 3 : 4,
                            .payload = data.frame,
                            .payload_len = sizeof(data.frame)};
  nfc_store_init();
  err = nfc_store_save(&saved);
  add_line(result, err == ESP_OK ? "Saved to NFC library" : "Could not save to NFC library");
  nfc_archive_field_t fields[] = {{"Frame", data.frame, sizeof(data.frame)}};
  hb_nfc_err_t archive_error = save_capture("Texkom", saved.uid, saved.uid_len, fields, 1, result);
  return archive_error == HB_NFC_OK ? err : archive_error;
}

static hb_nfc_err_t emulate_texkom(void) {
  if (!s_has_texkom)
    return HB_NFC_ERR_NOT_FOUND;
  esp_err_t err = hb_nfc_spi_prepare_target_tx();
  if (err != ESP_OK)
    return err;
  uint8_t mode = 0, control = 0;
  if (hb_nfc_spi_reg_read(ST25R3916_REG_MODE, &mode) != ESP_OK ||
      hb_nfc_spi_reg_read(ST25R3916_REG_OP_CTRL, &control) != ESP_OK)
    return HB_NFC_ERR_INTERNAL;
  err = hb_nfc_spi_reg_write(ST25R3916_REG_MODE, TEXKOM_PASSIVE_MODE);
  if (err == ESP_OK)
    err = hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, TEXKOM_PASSIVE_CONTROL);
  while (err == ESP_OK && !cancel_requested()) {
    err = texkom_emulate_frame(&s_texkom_last);
    vTaskDelay(pdMS_TO_TICKS(TEXKOM_EMULATION_GAP_MS) + 1);
  }
  esp_err_t restore = hb_nfc_spi_reg_write(ST25R3916_REG_MODE, mode);
  if (restore != ESP_OK)
    err = restore;
  restore = hb_nfc_spi_reg_write(ST25R3916_REG_OP_CTRL, control);
  if (restore != ESP_OK)
    err = restore;
  return err;
}

static esp_err_t v_reply(void *context,
                         const uint8_t *frame,
                         size_t length,
                         bool is_slot,
                         uint8_t *out,
                         size_t capacity,
                         size_t *out_length,
                         hb_nfc_v_codec_rate_t *out_rate) {
  iso15693_listener_t *listener = context;
  iso15693_listener_field(listener, true, (uint64_t)esp_timer_get_time());
  esp_err_t err =
      iso15693_listener_exchange(listener, frame, length, is_slot, out, capacity, out_length);
  *out_rate =
      listener->is_fast_rate
          ? (listener->is_high_rate ? HB_NFC_V_CODEC_RATE_FAST_HIGH : HB_NFC_V_CODEC_RATE_FAST_LOW)
          : (listener->is_high_rate ? HB_NFC_V_CODEC_RATE_HIGH : HB_NFC_V_CODEC_RATE_LOW);
  return err;
}

static hb_nfc_err_t emulate_v(void) {
  if (!s_has_v)
    return HB_NFC_ERR_NOT_FOUND;
  esp_err_t err = rf_frontend_reserve(RF_FRONTEND_NFC_13MHZ);
  if (err != ESP_OK)
    return err;
  highboy_nfc_config_t config = HIGHBOY_NFC_CONFIG_DEFAULT();
  err = highboy_nfc_field_off();
  if (err != ESP_OK)
    goto cleanup;
  err = iso15693_listener_init(&s_v_listener, &s_v_card);
  if (err != ESP_OK)
    goto cleanup;
  const uint8_t registers[][2] = {{ST25R3916_REG_MODE, TEXKOM_PASSIVE_MODE},
                                  {ST25R3916_REG_OP_CTRL, TEXKOM_PASSIVE_CONTROL},
                                  {ST25R3916_REG_RX_CONF1, NFC_V_RX_CONF1},
                                  {ST25R3916_REG_RX_CONF2, NFC_V_RX_CONF2},
                                  {ST25R3916_REG_RX_CONF3, 0},
                                  {ST25R3916_REG_RX_CONF4, 0}};
  for (size_t i = 0; i < sizeof(registers) / sizeof(registers[0]); i++) {
    err = hb_nfc_spi_reg_write(registers[i][0], registers[i][1]);
    if (err != ESP_OK)
      goto cleanup;
  }
  err = hb_nfc_spi_reg_write_b(ST25R3916_REG_B_CORR_CONF1, NFC_V_CORR_CONF1);
  if (err != ESP_OK)
    goto cleanup;
  err = hb_nfc_spi_reg_write_b(ST25R3916_REG_B_CORR_CONF2, NFC_V_CORR_CONF2);
  if (err != ESP_OK)
    goto cleanup;
  err = hb_nfc_spi_prepare_target_v(config.pin_irq);
  if (err != ESP_OK)
    goto cleanup;
  s_v_result_valid = true;
  while (!cancel_requested()) {
    err = hb_nfc_spi_target_v_exchange(v_reply, &s_v_listener);
    if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_RESPONSE || err == ESP_ERR_INVALID_SIZE) {
      uint8_t aux = 0;
      err = hb_nfc_spi_reg_read(ST25R3916_REG_AUX_DISPLAY, &aux);
      if (err == ESP_OK)
        iso15693_listener_field(
            &s_v_listener, (aux & ST25R3916_AUX_DISP_EFD_BIT) != 0, (uint64_t)esp_timer_get_time());
    }
    if (err != ESP_OK)
      break;
    vTaskDelay(pdMS_TO_TICKS(NFC_V_RETRY_MS));
  }
cleanup:
  while (hb_nfc_v_rx_deinit() != ESP_OK)
    vTaskDelay(pdMS_TO_TICKS(NFC_V_CLEANUP_MS));
  highboy_nfc_deinit();
  while (highboy_nfc_is_initialized()) {
    vTaskDelay(pdMS_TO_TICKS(NFC_V_CLEANUP_MS));
    highboy_nfc_deinit();
  }
  iso15693_listener_reset(&s_v_listener);
  esp_err_t release = rf_frontend_unreserve(RF_FRONTEND_NFC_13MHZ);
  return err == ESP_OK ? release : err;
}

static hb_nfc_err_t read_archive(nfc_protocol_result_t *result, bool is_export) {
  nfc_archive_t archive = {0};
  esp_err_t error = nfc_archive_load(s_archive_path, &archive);
  if (error != ESP_OK)
    return error;
  strlcpy(result->heading, archive.device, sizeof(result->heading));
  add_line(result, "UID %s", archive.uid);
  add_line(result, "%u fields, %u bytes", (unsigned)archive.fields, (unsigned)archive.length);
  add_line(result, archive.is_capture ? "Captured fields only" : "Flipper v4 archive");
  add_line(result, "Envelope checked; RF profile not validated");
  if (cancel_requested())
    error = ESP_ERR_INVALID_STATE;
  else if (is_export) {
    char path[NFC_ARCHIVE_PATH_SIZE];
    error = nfc_archive_export(&archive, path, sizeof(path));
    if (error == ESP_OK)
      add_line(result, "Copy: %s", strrchr(path, '/') + 1);
  }
  nfc_archive_free(&archive);
  return error;
}

hb_nfc_err_t nfc_protocol_service_archive(const char *filename) {
  if (!filename || !*filename || strchr(filename, '/') || strchr(filename, '\\') ||
      strstr(filename, "..") || strpbrk(filename, "\r\n"))
    return HB_NFC_ERR_PARAM;
  char path[NFC_ARCHIVE_PATH_SIZE];
  int length = snprintf(path, sizeof(path), NFC_ARCHIVE_DIRECTORY "/%s", filename);
  if (length < 0 || (size_t)length >= sizeof(path))
    return HB_NFC_ERR_PARAM;
  portENTER_CRITICAL(&s_mux);
  hb_nfc_err_t error = s_status.running ? ESP_ERR_INVALID_STATE : HB_NFC_OK;
  if (error == HB_NFC_OK)
    memcpy(s_archive_path, path, (size_t)length + 1);
  portEXIT_CRITICAL(&s_mux);
  return error;
}

static void protocol_task(void *arg) {
  const nfc_protocol_op_t operation = (nfc_protocol_op_t)(uintptr_t)arg;
  ESP_LOGI(TAG, "Starting %s", nfc_protocol_operation_name(operation));
  nfc_protocol_result_t result = {0};
  hb_nfc_err_t err = HB_NFC_ERR_INTERNAL;
  bool no_sleep = false;
  bool field = false;
  bool is_archive =
      operation == NFC_PROTOCOL_OP_ARCHIVE || operation == NFC_PROTOCOL_OP_ARCHIVE_EXPORT ||
      operation == NFC_PROTOCOL_OP_DESFIRE_LOAD || operation == NFC_PROTOCOL_OP_FELICA_LOAD ||
      operation == NFC_PROTOCOL_OP_NTAG4XX_LOAD || operation == NFC_PROTOCOL_OP_T4T_LOAD;
  if (is_archive) {
    err = operation == NFC_PROTOCOL_OP_T4T_LOAD       ? load_t4t(&result)
          : operation == NFC_PROTOCOL_OP_NTAG4XX_LOAD ? load_ntag4xx(&result)
          : operation == NFC_PROTOCOL_OP_FELICA_LOAD  ? load_felica(&result)
          : operation == NFC_PROTOCOL_OP_DESFIRE_LOAD
              ? load_desfire(&result)
              : read_archive(&result, operation == NFC_PROTOCOL_OP_ARCHIVE_EXPORT);
    goto done;
  }

  if (!highboy_nfc_is_initialized()) {
    highboy_nfc_config_t config = HIGHBOY_NFC_CONFIG_DEFAULT();
    if (highboy_nfc_init(&config) != ESP_OK)
      goto done;
  }
  if (power_manager_no_sleep_acquire() != ESP_OK)
    goto done;
  no_sleep = true;
  if (operation == NFC_PROTOCOL_OP_FELICA_EMULATE) {
    err = emulate_felica(&result);
    goto done;
  }
  if (operation == NFC_PROTOCOL_OP_T4T_EMULATE) {
    err = emulate_t4t(&result);
    goto done;
  }
  if (operation == NFC_PROTOCOL_OP_NTAG4XX_EMULATE) {
    err = emulate_ntag4xx();
    goto done;
  }
  if (operation == NFC_PROTOCOL_OP_DESFIRE_EMULATE) {
    err = emulate_desfire();
    goto done;
  }
  if (operation == NFC_PROTOCOL_OP_V_EMULATE) {
    err = emulate_v();
    goto done;
  }
  err = nfc_poller_start();
  if (err != HB_NFC_OK)
    goto done;
  field = true;
  vTaskDelay(pdMS_TO_TICKS(100));
  if (cancel_requested())
    goto done;

  switch (operation) {
    case NFC_PROTOCOL_OP_PLUS:
      err = read_plus(&result);
      break;
    case NFC_PROTOCOL_OP_UL_AES_WRITE:
      err = write_ul_aes(&result);
      break;
    case NFC_PROTOCOL_OP_PLUS_WRITE:
      err = write_plus(&result);
      break;
    case NFC_PROTOCOL_OP_DESFIRE:
      err = read_desfire(&result);
      break;
    case NFC_PROTOCOL_OP_ISO15693:
      err = read_iso15693(&result);
      break;
    case NFC_PROTOCOL_OP_ULTRALIGHT:
      err = read_ultralight(&result);
      break;
    case NFC_PROTOCOL_OP_T4T_WRITE:
      err = write_t4t(&result);
      break;
    case NFC_PROTOCOL_OP_T4T_CREATE:
      err = create_t4t(&result);
      break;
    case NFC_PROTOCOL_OP_NDEF:
      err = read_ndef(&result);
      break;
    case NFC_PROTOCOL_OP_ST25TB_WRITE:
      err = write_st25tb(&result);
      break;
    case NFC_PROTOCOL_OP_ST25TB:
      err = read_st25tb(&result);
      break;
    case NFC_PROTOCOL_OP_SLIX_WRITE:
      err = write_slix(&result);
      break;
    case NFC_PROTOCOL_OP_SLIX:
      err = read_slix(&result);
      break;
    case NFC_PROTOCOL_OP_NTAG4XX:
      err = read_ntag4xx(&result);
      break;
    case NFC_PROTOCOL_OP_TEXKOM_EMULATE:
      err = emulate_texkom();
      break;
    case NFC_PROTOCOL_OP_TEXKOM:
      err = read_texkom(&result);
      break;
    case NFC_PROTOCOL_OP_FELICA:
      err = read_felica(&result);
      break;
    default:
      err = HB_NFC_ERR_PARAM;
      break;
  }

done:
  if (operation == NFC_PROTOCOL_OP_PLUS_WRITE)
    s_plus_write_pending = false;
  if (operation == NFC_PROTOCOL_OP_UL_AES_WRITE)
    s_aes_write_pending = false;
  if (operation == NFC_PROTOCOL_OP_SLIX_WRITE)
    s_slix_write_pending = false;
  if (operation == NFC_PROTOCOL_OP_ST25TB_WRITE)
    s_st25tb_write_pending = false;
  if (field)
    nfc_poller_stop();
  else if (!is_archive && highboy_nfc_is_initialized())
    (void)highboy_nfc_field_off();
  if (no_sleep)
    (void)power_manager_no_sleep_release();

  const bool cancelled = cancel_requested() && operation != NFC_PROTOCOL_OP_FELICA_EMULATE &&
                         operation != NFC_PROTOCOL_OP_NTAG4XX_EMULATE &&
                         operation != NFC_PROTOCOL_OP_T4T_EMULATE;
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

hb_nfc_err_t nfc_protocol_service_plus_key(const char *text) {
  uint8_t key[MFP_BLOCK_SIZE] = {0};
  if (text) {
    if (strlen(text) != sizeof(key) * HEX_DIGITS_PER_BYTE)
      return HB_NFC_ERR_PARAM;
    for (size_t i = 0; i < sizeof(key) * HEX_DIGITS_PER_BYTE; i++) {
      unsigned digit;
      if (text[i] >= '0' && text[i] <= '9')
        digit = text[i] - '0';
      else if (text[i] >= 'a' && text[i] <= 'f')
        digit = text[i] - 'a' + HEX_ALPHA_VALUE;
      else if (text[i] >= 'A' && text[i] <= 'F')
        digit = text[i] - 'A' + HEX_ALPHA_VALUE;
      else {
        memset(key, 0, sizeof(key));
        return HB_NFC_ERR_PARAM;
      }
      key[i / HEX_DIGITS_PER_BYTE] = (key[i / HEX_DIGITS_PER_BYTE] << HEX_NIBBLE_BITS) | digit;
    }
  }
  portENTER_CRITICAL(&s_mux);
  hb_nfc_err_t err = s_status.running ? ESP_ERR_INVALID_STATE : HB_NFC_OK;
  if (err == HB_NFC_OK) {
    memcpy(s_plus_key, key, sizeof(key));
    s_has_plus_key = text != NULL;
  }
  portEXIT_CRITICAL(&s_mux);
  memset(key, 0, sizeof(key));
  return err;
}

hb_nfc_err_t nfc_protocol_service_plus_write(const char *filename) {
  bool is_encrypted = true;
  if (filename && strncmp(filename, "plain:", 6) == 0) {
    filename += 6;
    is_encrypted = false;
  }
  char path[PLUS_PATH_SIZE] = {0};
  if (filename && *filename) {
    if (strchr(filename, '/') || strstr(filename, "..") || strchr(filename, '\n') ||
        strchr(filename, '\r'))
      return HB_NFC_ERR_PARAM;
    int length = snprintf(path, sizeof(path), UL_AES_DUMP_DIR "/%s", filename);
    if (length < 0 || (size_t)length >= sizeof(path))
      return HB_NFC_ERR_PARAM;
  }
  portENTER_CRITICAL(&s_mux);
  hb_nfc_err_t err = HB_NFC_OK;
  if (s_status.running)
    err = ESP_ERR_INVALID_STATE;
  else if (filename && !*filename && !s_plus_last_path[0])
    err = ESP_ERR_INVALID_STATE;
  else {
    if (filename) {
      const char *selected = *filename ? path : s_plus_last_path;
      memcpy(s_plus_write_path, selected, strlen(selected) + 1);
    }
    s_plus_write_encrypted = is_encrypted;
    s_plus_write_pending = filename != NULL;
  }
  portEXIT_CRITICAL(&s_mux);
  return err;
}

hb_nfc_err_t nfc_protocol_service_st25tb_write(const char *hex) {
  uint8_t bytes[1 + ST25TB_WRITE_DATA_SIZE] = {0};
  if (hex) {
    if (strlen(hex) != ST25TB_WRITE_INPUT_SIZE || hex[ST25TB_WRITE_SEPARATOR] != ':')
      return HB_NFC_ERR_PARAM;
    size_t nibble = 0;
    for (size_t i = 0; i < ST25TB_WRITE_INPUT_SIZE; i++) {
      if (i == ST25TB_WRITE_SEPARATOR)
        continue;
      uint8_t value;
      if (hex[i] >= '0' && hex[i] <= '9')
        value = hex[i] - '0';
      else if (hex[i] >= 'a' && hex[i] <= 'f')
        value = hex[i] - 'a' + HEX_ALPHA_VALUE;
      else if (hex[i] >= 'A' && hex[i] <= 'F')
        value = hex[i] - 'A' + HEX_ALPHA_VALUE;
      else
        return HB_NFC_ERR_PARAM;
      bytes[nibble / HEX_DIGITS_PER_BYTE] =
          (uint8_t)((bytes[nibble / HEX_DIGITS_PER_BYTE] << HEX_NIBBLE_BITS) | value);
      nibble++;
    }
  }
  portENTER_CRITICAL(&s_mux);
  hb_nfc_err_t err = HB_NFC_OK;
  if (s_status.running) {
    err = ESP_ERR_INVALID_STATE;
  } else if (hex && (!s_has_st25tb || bytes[0] >= s_st25tb_blocks)) {
    err = HB_NFC_ERR_PARAM;
  } else {
    s_st25tb_write_pending = hex != NULL;
    s_st25tb_write_block = bytes[0];
    memcpy(s_st25tb_write_data, bytes + 1, ST25TB_WRITE_DATA_SIZE);
  }
  portEXIT_CRITICAL(&s_mux);
  return err;
}

hb_nfc_err_t nfc_protocol_service_ul_aes_write(const char *filename) {
  bool copy_key = filename && strncmp(filename, "key:", 4) == 0;
  if (copy_key)
    filename += 4;
  char path[UL_AES_PATH_SIZE] = {0};
  if (filename && *filename) {
    if (strchr(filename, '/') || strstr(filename, "..") || strchr(filename, '\n') ||
        strchr(filename, '\r'))
      return HB_NFC_ERR_PARAM;
    int length = snprintf(path, sizeof(path), UL_AES_DUMP_DIR "/%s", filename);
    if (length < 0 || (size_t)length >= sizeof(path))
      return HB_NFC_ERR_PARAM;
  }
  portENTER_CRITICAL(&s_mux);
  hb_nfc_err_t error = HB_NFC_OK;
  if (s_status.running)
    error = ESP_ERR_INVALID_STATE;
  else if (filename && !*filename && !s_aes_last_path[0])
    error = ESP_ERR_INVALID_STATE;
  else {
    if (filename) {
      const char *selected = *filename ? path : s_aes_last_path;
      memcpy(s_aes_write_path, selected, strlen(selected) + 1);
    }
    s_aes_write_pending = filename != NULL;
    s_aes_copy_key = copy_key;
  }
  portEXIT_CRITICAL(&s_mux);
  return error;
}

hb_nfc_err_t nfc_protocol_service_ul_aes_key(const char *text) {
  uint8_t key[T2T_AES_BLOCK_SIZE] = {0};
  hb_nfc_err_t error = HB_NFC_ERR_PARAM;
  if (text) {
    if (strlen(text) != 2 + sizeof(key) * HEX_DIGITS_PER_BYTE ||
        (text[0] != '0' && text[0] != '1') || text[1] != ':')
      goto cleanup;
    for (size_t i = 0; i < sizeof(key) * HEX_DIGITS_PER_BYTE; i++) {
      char c = text[i + 2];
      uint8_t digit;
      if (c >= '0' && c <= '9')
        digit = (uint8_t)(c - '0');
      else if (c >= 'a' && c <= 'f')
        digit = (uint8_t)(c - 'a' + HEX_ALPHA_VALUE);
      else if (c >= 'A' && c <= 'F')
        digit = (uint8_t)(c - 'A' + HEX_ALPHA_VALUE);
      else
        goto cleanup;
      key[i / HEX_DIGITS_PER_BYTE] =
          (uint8_t)((key[i / HEX_DIGITS_PER_BYTE] << HEX_NIBBLE_BITS) | digit);
    }
  }
  portENTER_CRITICAL(&s_mux);
  if (s_status.running)
    error = ESP_ERR_INVALID_STATE;
  else {
    memcpy(s_ul_aes_key, key, sizeof(key));
    s_ul_aes_secure = text && text[0] == '1';
    s_has_ul_aes_key = text != NULL;
    error = HB_NFC_OK;
  }
  portEXIT_CRITICAL(&s_mux);
cleanup:
  memset(key, 0, sizeof(key));
  return error;
}

hb_nfc_err_t nfc_protocol_service_slix_write(const char *filename) {
  char path[UL_AES_PATH_SIZE] = {0};
  if (filename && *filename) {
    if (strchr(filename, '/') || strstr(filename, "..") || strchr(filename, '\n') ||
        strchr(filename, '\r'))
      return HB_NFC_ERR_PARAM;
    int length = snprintf(path, sizeof(path), UL_AES_DUMP_DIR "/%s", filename);
    if (length < 0 || (size_t)length >= sizeof(path))
      return HB_NFC_ERR_PARAM;
  }
  portENTER_CRITICAL(&s_mux);
  hb_nfc_err_t error = HB_NFC_OK;
  if (s_status.running)
    error = ESP_ERR_INVALID_STATE;
  else if (filename && !*filename && !s_slix_last_path[0])
    error = ESP_ERR_INVALID_STATE;
  else {
    if (filename) {
      const char *selected = *filename ? path : s_slix_last_path;
      memcpy(s_slix_write_path, selected, strlen(selected) + 1);
    }
    s_slix_write_pending = filename != NULL;
  }
  portEXIT_CRITICAL(&s_mux);
  return error;
}

hb_nfc_err_t nfc_protocol_service_slix_password(const char *hex) {
  slix_password_t type = SLIX_PASSWORD_PRIVACY;
  if (hex && strlen(hex) == SLIX_PASSWORD_BYTES * HEX_DIGITS_PER_BYTE + 2 && hex[1] == ':') {
    const char *types = "rwpde";
    const char *found = strchr(types, hex[0]);
    if (!found)
      return HB_NFC_ERR_PARAM;
    type = (slix_password_t)(found - types);
    hex += 2;
  }
  uint8_t password[SLIX_PASSWORD_BYTES] = {0};
  if (hex != NULL) {
    if (strlen(hex) != sizeof(password) * 2)
      return HB_NFC_ERR_PARAM;
    for (size_t i = 0; i < sizeof(password) * 2; i++) {
      uint8_t digit;
      if (hex[i] >= '0' && hex[i] <= '9')
        digit = (uint8_t)(hex[i] - '0');
      else if (hex[i] >= 'a' && hex[i] <= 'f')
        digit = (uint8_t)(hex[i] - 'a' + 10);
      else if (hex[i] >= 'A' && hex[i] <= 'F')
        digit = (uint8_t)(hex[i] - 'A' + 10);
      else
        return HB_NFC_ERR_PARAM;
      password[i / 2] = (uint8_t)((password[i / 2] << 4) | digit);
    }
  }
  portENTER_CRITICAL(&s_mux);
  if (s_status.running) {
    portEXIT_CRITICAL(&s_mux);
    return ESP_ERR_INVALID_STATE;
  }
  if (hex) {
    memcpy(s_slix_password[type], password, sizeof(password));
    s_has_slix_password[type] = true;
  } else {
    memset(s_slix_password, 0, sizeof(s_slix_password));
    memset(s_has_slix_password, 0, sizeof(s_has_slix_password));
  }
  portEXIT_CRITICAL(&s_mux);
  memset(password, 0, sizeof(password));
  return HB_NFC_OK;
}

hb_nfc_err_t nfc_protocol_service_select_texkom(int index) {
  nfc_store_entry_t entry;
  hb_nfc_err_t err = nfc_store_load(index, &entry);
  if (err != HB_NFC_OK)
    return err;
  if (entry.protocol != HB_PROTO_TEXKOM || entry.payload_len != TEXKOM_FRAME_SIZE)
    return ESP_ERR_INVALID_RESPONSE;
  texkom_data_t data = {0};
  memcpy(data.frame, entry.payload, sizeof(data.frame));
  uint16_t cycles[TEXKOM_MAX_GAPS + 1];
  bool valid = false;
  for (data.type = 0; data.type < TEXKOM_TYPE_COUNT; data.type++) {
    size_t count = TEXKOM_MAX_GAPS + 1;
    if (texkom_encode(&data, cycles, &count) == ESP_OK) {
      valid = true;
      break;
    }
  }
  if (!valid)
    return ESP_ERR_INVALID_RESPONSE;
  portENTER_CRITICAL(&s_mux);
  if (s_status.running) {
    portEXIT_CRITICAL(&s_mux);
    return ESP_ERR_INVALID_STATE;
  }
  s_texkom_last = data;
  s_has_texkom = true;
  portEXIT_CRITICAL(&s_mux);
  return HB_NFC_OK;
}

hb_nfc_err_t nfc_protocol_service_select_v(int index) {
  nfc_store_entry_t entry;
  hb_nfc_err_t err = nfc_store_load(index, &entry);
  if (err != HB_NFC_OK)
    return err;
  if ((entry.protocol != HB_PROTO_ISO15693 && entry.protocol != HB_PROTO_SLIX) ||
      entry.uid_len != sizeof(s_v_card.uid) || entry.payload_len < NFC_V_DUMP_HEADER_SIZE ||
      entry.payload[0] != NFC_V_DUMP_VERSION)
    return ESP_ERR_INVALID_RESPONSE;
  uint16_t blocks = entry.payload[1] | ((uint16_t)entry.payload[2] << NFC_V_BYTE_BITS);
  uint8_t size = entry.payload[3];
  if (!blocks || blocks > ISO15693_LISTENER_BLOCKS || !size ||
      size > ISO15693_LISTENER_BLOCK_BYTES ||
      entry.payload_len != NFC_V_DUMP_HEADER_SIZE + (size_t)blocks * size)
    return ESP_ERR_INVALID_RESPONSE;
  portENTER_CRITICAL(&s_mux);
  if (s_status.running) {
    portEXIT_CRITICAL(&s_mux);
    return ESP_ERR_INVALID_STATE;
  }
  memset(&s_v_card, 0, sizeof(s_v_card));
  memcpy(s_v_card.uid, entry.uid, sizeof(s_v_card.uid));
  s_v_card.block_count = blocks;
  s_v_card.block_size = size;
  s_v_card.dsfid = entry.payload[4];
  s_v_card.afi = entry.payload[5];
  s_v_card.ic_ref = entry.payload[6];
  s_v_card.is_slix = entry.protocol == HB_PROTO_SLIX;
  s_v_card.is_read_only = true;
  memcpy(s_v_card.memory, entry.payload + NFC_V_DUMP_HEADER_SIZE, (size_t)blocks * size);
  s_has_v = true;
  s_v_result_valid = false;
  portEXIT_CRITICAL(&s_mux);
  return HB_NFC_OK;
}

hb_nfc_err_t nfc_protocol_service_select_v_file(const char *path, bool allow_writes) {
  iso15693_listener_card_t *card = malloc(sizeof(*card));
  if (!card) {
    ESP_LOGE(TAG, "Cannot allocate NFC-V profile");
    return ESP_ERR_NO_MEM;
  }
  esp_err_t error = nfc_v_file_load(path, card);
  if (error != ESP_OK)
    goto cleanup;
  portENTER_CRITICAL(&s_mux);
  if (s_status.running) {
    error = ESP_ERR_INVALID_STATE;
  } else {
    s_v_card = *card;
    s_v_card.is_read_only = !allow_writes;
    s_has_v = true;
    s_v_result_valid = false;
  }
  portEXIT_CRITICAL(&s_mux);
cleanup:
  memset(card, 0, sizeof(*card));
  free(card);
  return error;
}

hb_nfc_err_t nfc_protocol_service_start(nfc_protocol_op_t operation) {
  if ((int)operation < 0 || operation >= NFC_PROTOCOL_OP_COUNT)
    return HB_NFC_ERR_PARAM;
  portENTER_CRITICAL(&s_mux);
  if (s_status.running) {
    portEXIT_CRITICAL(&s_mux);
    return ESP_ERR_INVALID_STATE;
  }
  s_v_result_valid = false;
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

esp_err_t nfc_protocol_service_get_v_result(iso15693_listener_card_t *out_card) {
  if (out_card == NULL)
    return ESP_ERR_INVALID_ARG;
  esp_err_t error = ESP_ERR_INVALID_STATE;
  portENTER_CRITICAL(&s_mux);
  if (!s_status.running && s_has_v && s_v_result_valid &&
      s_status.operation == NFC_PROTOCOL_OP_V_EMULATE) {
    *out_card = s_v_card;
    error = ESP_OK;
  }
  portEXIT_CRITICAL(&s_mux);
  return error;
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
    case NFC_PROTOCOL_OP_ARCHIVE:
    case NFC_PROTOCOL_OP_ARCHIVE_EXPORT:
      return "NFC archives";
    case NFC_PROTOCOL_OP_PLUS:
    case NFC_PROTOCOL_OP_PLUS_WRITE:
      return "MIFARE Plus";
    case NFC_PROTOCOL_OP_DESFIRE_LOAD:
    case NFC_PROTOCOL_OP_DESFIRE_EMULATE:
    case NFC_PROTOCOL_OP_DESFIRE:
      return "DESFire";
    case NFC_PROTOCOL_OP_ISO15693:
      return "NFC-V / ISO15693";
    case NFC_PROTOCOL_OP_UL_AES_WRITE:
    case NFC_PROTOCOL_OP_ULTRALIGHT:
      return "Ultralight / NTAG";
    case NFC_PROTOCOL_OP_NDEF:
      return "NDEF";
    case NFC_PROTOCOL_OP_ST25TB_WRITE:
      return "ST25TB write";
    case NFC_PROTOCOL_OP_ST25TB:
      return "ST25TB";
    case NFC_PROTOCOL_OP_SLIX_WRITE:
    case NFC_PROTOCOL_OP_SLIX:
      return "ICODE SLIX";
    case NFC_PROTOCOL_OP_T4T_LOAD:
      return "Type 4 load";
    case NFC_PROTOCOL_OP_T4T_EMULATE:
      return "Type 4 emulation";
    case NFC_PROTOCOL_OP_T4T_WRITE:
      return "Type 4 write";
    case NFC_PROTOCOL_OP_T4T_CREATE:
      return "Type 4 create";
    case NFC_PROTOCOL_OP_NTAG4XX_LOAD:
      return "NTAG4xx load";
    case NFC_PROTOCOL_OP_NTAG4XX_EMULATE:
      return "NTAG4xx identity";
    case NFC_PROTOCOL_OP_NTAG4XX:
      return "NTAG4xx";
    case NFC_PROTOCOL_OP_TEXKOM_EMULATE:
      return "Texkom emulation";
    case NFC_PROTOCOL_OP_V_EMULATE:
      return "NFC-V / SLIX emulation";
    case NFC_PROTOCOL_OP_TEXKOM:
      return "Texkom";
    case NFC_PROTOCOL_OP_FELICA_LOAD:
    case NFC_PROTOCOL_OP_FELICA_EMULATE:
    case NFC_PROTOCOL_OP_FELICA:
      return "FeliCa";
    default:
      return "NFC";
  }
}
