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

#include "nfc_desfire.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "TEST_DESFIRE";
static unsigned s_mode, s_calls, s_reads;
static bool s_cancel;

int storage_write_atomic(const char *path, const void *data, size_t size) {
  FILE *file = fopen(path, "wb");
  if (!file) return ESP_FAIL;
  bool ok = fwrite(data, 1, size, file) == size;
  if (fclose(file)) ok = false;
  return ok ? ESP_OK : ESP_FAIL;
}

int mf_desfire_poller_init(const nfc_iso14443a_data_t *identity, mf_desfire_session_t *session) {
  (void)identity;
  session->dep.ats_len = 6;
  const uint8_t ats[] = {6,0x75,0x77,0x81,0x02,0x80};
  memcpy(session->dep.ats, ats, sizeof(ats)); return ESP_OK;
}

int mf_desfire_transceive_native(mf_desfire_session_t *session, uint8_t cmd,
                                 const uint8_t *params, size_t params_length,
                                 uint8_t *out, size_t capacity, size_t *length, uint16_t *status) {
  s_calls++; *status = MF_DESFIRE_SW_OK; *length = 0;
  uint8_t response[512] = {0}; size_t count = 0;
  switch (cmd) {
    case 0x60:
      if (s_mode == 1 && !session->use_native_frames) return ESP_FAIL;
      count = 28; response[1] = 1; response[3] = 3; break;
    case 0x6E:
      if (s_mode == 2) { *status = 0x911C; break; }
      count = 3; response[1] = 0x10; break;
    case 0x45: count = 2; response[0] = 0x0F; response[1] = 0x82; break;
    case 0x64:
      assert(params_length == 1); count = 1; response[0] = params[0] + 10; break;
    case 0x6A:
      if (s_mode == 2) { *status = 0x919D; break; }
      count = 3; response[0] = 0x12; response[1] = 0x34; response[2] = 0x56; break;
    case 0x5A: assert(params_length == 3 && params[0] == 0x12); break;
    case 0x6F:
      count = 6; for (size_t i = 0; i < count; i++) response[i] = i; break;
    case 0xF5: {
      assert(params_length == 1);
      response[0] = params[0]; response[2] = 0xEE; response[3] = 0xEE;
      if (params[0] < 2) { count = 7; response[4] = 0x58; response[5] = 2; }
      else if (params[0] == 2) { count = 17; response[8] = 100; response[16] = 1; }
      else if (params[0] < 5) { count = 13; response[4] = 4; response[7] = 5; response[10] = 2; }
      else { count = 10; response[1] = 0x20; response[4] = 2; response[9] = 10; }
      if (s_mode == 3 && params[0] == 1) *status = 0x919D;
      if (s_mode == 4) count = 5;
      break;
    }
    case 0xBD:
      assert(params_length == 7); count = params[4] | (params[5] << 8);
      if (s_mode == 3) { *status = 0x91AE; break; }
      for (size_t i = 0; i < count; i++) response[i] = (params[1] + i) & 0xFF;
      s_reads++; break;
    case 0x6C: assert(params_length == 1); count = 4; response[0] = 42; s_reads++; break;
    case 0xBB:
      assert(params_length == 7 && params[4] == 1); count = 4;
      memset(response, params[1] ? 0xBB : 0xAA, count); s_reads++; break;
    default: assert(false);
  }
  assert(count <= capacity); memcpy(out, response, count); *length = count; return ESP_OK;
}

static bool cancelled(void) { return s_cancel; }

static void corrupt(const char *path, const char *suffix) {
  FILE *file = fopen(path, "ab"); assert(file);
  assert(fwrite(suffix, 1, strlen(suffix), file) == strlen(suffix)); assert(!fclose(file));
}

static void replace_field(const char *path, const char *replacement) {
  FILE *file = fopen(path, "rb"); assert(file);
  assert(!fseek(file, 0, SEEK_END)); long size = ftell(file); assert(size > 0); rewind(file);
  char *text = malloc((size_t)size + 1); assert(text);
  assert(fread(text, 1, size, file) == (size_t)size); text[size] = 0; fclose(file);
  file = fopen(path, "wb"); assert(file);
  const char *separator = strchr(replacement, ':'); assert(separator);
  size_t key_length = (size_t)(separator - replacement) + 1;
  char *line = text; bool replaced = false;
  while (line && *line) {
    char *next = strchr(line, '\n'); if (next) *next++ = 0;
    if (!strncmp(line, replacement, key_length)) {
      assert(fputs(replacement, file) >= 0); replaced = true;
    } else assert(fprintf(file, "%s\n", line) >= 0);
    line = next;
  }
  assert(replaced); assert(!fclose(file)); free(text);
}

static void remove_extensions(const char *path) {
  FILE *file = fopen(path, "rb"); assert(file);
  assert(!fseek(file, 0, SEEK_END)); long size = ftell(file); assert(size > 0); rewind(file);
  char *text = malloc((size_t)size + 1); assert(text);
  assert(fread(text, 1, size, file) == (size_t)size); text[size] = 0; fclose(file);
  file = fopen(path, "wb"); assert(file);
  char *line = text;
  while (line && *line) {
    char *next = strchr(line, '\n'); if (next) *next++ = 0;
    if (!strstr(line, "Highboy") && !strstr(line, " Known:") && !strstr(line, " Flags:"))
      assert(fprintf(file, "%s\n", line) >= 0);
    line = next;
  }
  assert(!fclose(file)); free(text);
}

static const char *s_fork_fixture =
    "# Copyright (c) 2025 HIGH CODE LLC\n"
    "#\n"
    "# TentacleOS is free software: you can redistribute it and/or modify\n"
    "# it under the terms of the GNU General Public License as published by\n"
    "# the Free Software Foundation, either version 3 of the License, or\n"
    "# (at your option) any later version.\n"
    "#\n"
    "# TentacleOS is distributed in the hope that it will be useful,\n"
    "# but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n"
    "# GNU General Public License for more details.\n"
    "#\n"
    "# You should have received a copy of the GNU General Public License\n"
    "# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.\n"
    "Filetype: Flipper NFC device\n"
    "Version: 4\n"
    "Device type: Mifare DESFire\n"
    "UID: 04 01 02 03 04 05 06\n"
    "ATQA: 03 44\n"
    "SAK: 20\n"
    "PICC Version: 04 01 01 12 00 1A 05 04 01 01 12 00 1A 05 04 01 02 03 04 05 06 01 02 03 04 05 12 26\n"
    "PICC Change Key ID: 00\n"
    "PICC Config Changeable: true\n"
    "PICC Free Create Delete: true\n"
    "PICC Free Directory List: true\n"
    "PICC Key Changeable: true\n"
    "PICC Max Keys: 01\n"
    "PICC Key 0 Version: AA\n"
    "Application Count: 1\n"
    "Application IDs: A1 B2 C3\n"
    "Application a1b2c3 Change Key ID: 0F\n"
    "Application a1b2c3 Config Changeable: false\n"
    "Application a1b2c3 Free Create Delete: false\n"
    "Application a1b2c3 Free Directory List: true\n"
    "Application a1b2c3 Key Changeable: false\n"
    "Application a1b2c3 Max Keys: 00\n"
    "Application a1b2c3 File IDs: 05\n"
    "Application a1b2c3 File 5 Type: 02\n"
    "Application a1b2c3 File 5 Communication Settings: 00\n"
    "Application a1b2c3 File 5 Access Rights: EE EE\n"
    "Application a1b2c3 File 5 Hi Limit: 100\n"
    "Application a1b2c3 File 5 Lo Limit: 4294967196\n"
    "Application a1b2c3 File 5 Limited Credit Value: 3\n"
    "Application a1b2c3 File 5 Limited Credit Enabled: true\n"
    "Application a1b2c3 File 5: FE FF FF FF\n";

int main(int argc, char **argv) {
  (void)TAG; assert(argc == 2);
  nfc_iso14443a_data_t identity = {.uid = {1,2,3,4,5,6,7}, .uid_len = 7, .sak = 0x20, .atqa = {0x44,0}};
  nfc_desfire_card_t card = {0}, loaded = {0};
  assert(storage_write_atomic(argv[1], s_fork_fixture, strlen(s_fork_fixture)) == ESP_OK);
  assert(nfc_desfire_load(argv[1], &loaded) == ESP_OK);
  assert(loaded.identity.ats_len == 1 && loaded.identity.atqa[0] == 0x44 && loaded.identity.atqa[1] == 3);
  assert(loaded.keys.versions[0] == 0xAA && !loaded.has_free_memory);
  assert(loaded.apps[0].files[0].low == (uint32_t)-100 && loaded.apps[0].files[0].data[0] == 0xFE);
  nfc_desfire_free(&loaded);
  assert(nfc_desfire_read(&identity, &card, cancelled) == ESP_OK);
  assert(card.has_free_memory && card.free_memory == 4096 && card.app_count == 1);
  assert(card.keys.flags == 8 && card.keys.versions[1] == 11);
  assert(card.apps[0].file_count == 6 && s_reads == 9);
  assert(card.apps[0].files[0].length == 600 && card.apps[0].files[0].data[599] == (599 & 255));
  assert(card.apps[0].files[2].data[0] == 42);
  assert(card.apps[0].files[3].length == 8 && card.apps[0].files[3].data[4] == 0xBB);
  assert(card.apps[0].files[5].counter_limit == 10 && !card.apps[0].files[5].has_data);
  assert(nfc_desfire_save(argv[1], &card) == ESP_OK);
  assert(nfc_desfire_load(argv[1], &loaded) == ESP_OK);
  assert(!memcmp(&card.identity, &loaded.identity, sizeof(card.identity)));
  assert(loaded.keys.flags == 8 && loaded.apps[0].files[0].length == 600);
  assert(!memcmp(loaded.apps[0].files[0].data, card.apps[0].files[0].data, 600));
  assert(loaded.apps[0].files[5].counter_limit == 10);
  char page[512]; nfc_desfire_page(&loaded, 4, page, sizeof(page)); assert(strstr(page, "File 0"));
  assert(nfc_desfire_page_count(&loaded) >= 8);
  nfc_desfire_free(&loaded);
  remove_extensions(argv[1]);
  assert(nfc_desfire_load(argv[1], &loaded) == ESP_OK);
  assert(loaded.apps[0].files[3].length == 8 && loaded.keys.flags == 0);
  nfc_desfire_free(&loaded);
  corrupt(argv[1], "UID: 01 02 03 04\n");
  assert(nfc_desfire_load(argv[1], &loaded) != ESP_OK && !loaded.apps);
  nfc_desfire_free(&card);
  for (s_mode = 1; s_mode <= 3; s_mode++) {
    assert(nfc_desfire_read(&identity, &card, cancelled) == ESP_OK);
    if (s_mode == 2) assert(!card.has_apps && !card.has_free_memory && !card.app_count);
    if (s_mode == 3) {
      assert(!card.apps[0].files[0].has_data && !card.apps[0].files[1].has_settings);
    }
    assert(nfc_desfire_save(argv[1], &card) == ESP_OK);
    assert(nfc_desfire_load(argv[1], &loaded) == ESP_OK);
    if (s_mode == 3) assert(!loaded.apps[0].files[0].has_data && !loaded.apps[0].files[1].has_settings);
    nfc_desfire_free(&card); nfc_desfire_free(&loaded);
  }
  assert(nfc_desfire_read(&identity, &card, cancelled) != ESP_OK && !card.apps);
  const char *invalid[] = {
    "Application Count: 4294967296\n", "Application IDs: 12 34 56 12 34 56\n",
    "PICC Max Keys: FF\n", "Application 123456 File IDs: 00 00\n",
    "Application 123456 File 0 Size: 4294967295\n", "UID: 01 02 03 04 05\n",
    "Application 123456 File 3 Cur: 4294967295\n"
  };
  s_mode = 0;
  assert(nfc_desfire_read(&identity, &card, cancelled) == ESP_OK);
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    assert(nfc_desfire_save(argv[1], &card) == ESP_OK); replace_field(argv[1], invalid[i]);
    assert(nfc_desfire_load(argv[1], &loaded) != ESP_OK && !loaded.apps);
  }
  assert(nfc_desfire_save("/nonexistent-desfire/card.nfc", &card) != ESP_OK);
  nfc_desfire_free(&card);
  s_mode = 0; s_cancel = true;
  assert(nfc_desfire_read(&identity, &card, cancelled) == ESP_ERR_INVALID_STATE && !card.apps);
  nfc_desfire_file_t file = {0};
  const uint8_t rights[] = {0,0x80,0x12,0x34,1,0,0,1,0x56,0x78};
  assert(nfc_desfire_parse_settings(rights, sizeof(rights), &file) == ESP_OK);
  assert(file.rights_length == 4 && file.rights[2] == 0x56);
  for (size_t size = 0; size < sizeof(rights); size++)
    assert(nfc_desfire_parse_settings(rights, size, &file) != ESP_OK);
  puts("DESFire discovery, file types, fallback, partial data, cancellation and typed round trips passed");
  return 0;
}
