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

#include "nfc_felica.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "storage_atomic.h"

#define TEXT_LIMIT         (1024U * 1024U)
#define FIELD_LIMIT        25000U
#define PREFIX_SIZE        16U
#define KEY_SIZE           96U
#define HEX_BASE           16U
#define DEC_BASE           10U
#define FORMAT_VERSION     4U
#define OLD_KEY_VERSION    3U
#define SYSTEM_KEY_VERSION 4U
#define HEX_BYTE_TEXT      3U
#define AREA_VALUES        5U

typedef struct {
  char key[KEY_SIZE];
  char *value;
} nfc_felica_field_t;
typedef struct {
  char *text;
  nfc_felica_field_t *fields;
  size_t count, used;
} nfc_felica_document_t;
static const char *TAG = "NFC_FELICA_FILE";

static char *trim(char *text) {
  while (isspace((unsigned char)*text))
    text++;
  size_t size = strlen(text);
  while (size && isspace((unsigned char)text[size - 1]))
    text[--size] = 0;
  return text;
}
static int compare(const void *left, const void *right) {
  return strcmp(((const nfc_felica_field_t *)left)->key, ((const nfc_felica_field_t *)right)->key);
}
static const char *get(const nfc_felica_document_t *doc, const char *key) {
  nfc_felica_field_t query = {0};
  if (strlen(key) >= sizeof(query.key))
    return NULL;
  memcpy(query.key, key, strlen(key) + 1);
  const nfc_felica_field_t *field =
      bsearch(&query, doc->fields, doc->count, sizeof(query), compare);
  return field ? field->value : NULL;
}
static bool number(const nfc_felica_document_t *doc, const char *key, size_t *out, size_t limit) {
  const char *value = get(doc, key);
  char *end;
  if (!value || !isdigit((unsigned char)*value))
    return false;
  errno = 0;
  unsigned long parsed = strtoul(value, &end, DEC_BASE);
  if (errno || *end || parsed > limit)
    return false;
  *out = parsed;
  return true;
}
static bool bytes(const char *value, uint8_t *out, size_t count, bool has_bar) {
  if (!value)
    return false;
  for (size_t i = 0; i < count; i++) {
    while (*value == ' ')
      value++;
    if (!isxdigit((unsigned char)value[0]) || !isxdigit((unsigned char)value[1]) ||
        (value[2] && !isspace((unsigned char)value[2]) && value[2] != '|'))
      return false;
    char pair[] = {value[0], value[1], 0};
    out[i] = strtoul(pair, NULL, HEX_BASE);
    value += 2;
  }
  while (isspace((unsigned char)*value))
    value++;
  if (has_bar && *value++ != '|')
    return false;
  while (isspace((unsigned char)*value))
    value++;
  return !*value;
}
static bool flag(const nfc_felica_document_t *doc, const char *key, bool *out) {
  const char *value = get(doc, key);
  if (!value)
    return true;
  if (strcmp(value, "true") && strcmp(value, "false"))
    return false;
  *out = !strcmp(value, "true");
  return true;
}
static void *allocate(size_t count, size_t size) {
  if (!count)
    return NULL;
  void *out = calloc(count, size);
  if (!out)
    ESP_LOGE(TAG, "Cannot allocate profile");
  return out;
}
static bool parse_system(nfc_felica_document_t *doc,
                         nfc_felica_system_t *sys,
                         size_t index,
                         size_t version,
                         size_t *total) {
  char prefix[PREFIX_SIZE], key[KEY_SIZE];
  snprintf(prefix, sizeof(prefix), "System %02X", (unsigned)index);
  const char *value = get(doc, prefix);
  unsigned code = 0, kv = NFC_FELICA_UNKNOWN;
  if (!value)
    return false;
  int end = 0;
  if (version >= OLD_KEY_VERSION) {
    if (sscanf(value, "%4X | Key version %4X |%n", &code, &kv, &end) != 2 || !end || value[end])
      return false;
  } else if (sscanf(value, "%4X%n", &code, &end) != 1 || value[end])
    return false;
  sys->code = code;
  sys->key_version = version >= SYSTEM_KEY_VERSION ? kv : NFC_FELICA_UNKNOWN;
  snprintf(key, sizeof(key), "%s Area found", prefix);
  if (!number(doc, key, &sys->area_count, NFC_FELICA_NODES))
    return false;
  sys->areas = allocate(sys->area_count, sizeof(*sys->areas));
  if (sys->area_count && !sys->areas)
    return false;
  for (size_t i = 0; i < sys->area_count; i++) {
    snprintf(key, sizeof(key), "%s Area %03X", prefix, (unsigned)i);
    value = get(doc, key);
    unsigned a = 0, b = NFC_FELICA_UNKNOWN, c = 0, d = 0, e = NFC_FELICA_UNKNOWN;
    end = 0;
    if (!value)
      return false;
    if (version >= OLD_KEY_VERSION) {
      if (sscanf(value,
                 "| Code %4X | End %4X | Services #%3X-#%3X | Key version %4X |%n",
                 &a,
                 &b,
                 &c,
                 &d,
                 &e,
                 &end) != AREA_VALUES ||
          !end || value[end])
        return false;
    } else if (sscanf(value, "| Code %4X | Services #%3X-#%3X |%n", &a, &c, &d, &end) != 3 ||
               !end || value[end])
      return false;
    sys->areas[i] = (nfc_felica_area_t){a, b, c, d, e};
  }
  snprintf(key, sizeof(key), "%s Service found", prefix);
  if (!number(doc, key, &sys->service_count, NFC_FELICA_NODES - sys->area_count))
    return false;
  sys->services = allocate(sys->service_count, sizeof(*sys->services));
  if (sys->service_count && !sys->services)
    return false;
  for (size_t i = 0; i < sys->service_count; i++) {
    snprintf(key, sizeof(key), "%s Service %03X", prefix, (unsigned)i);
    value = get(doc, key);
    if (!value || sscanf(value, "| Code %4X |", &code) != 1)
      return false;
    kv = NFC_FELICA_UNKNOWN;
    if (version >= OLD_KEY_VERSION) {
      const char *tail = strstr(value, "Key version ");
      end = 0;
      if (!tail || sscanf(tail, "Key version %4X |%n", &kv, &end) != 1 || !end || tail[end])
        return false;
    }
    for (size_t j = 0; j < i; j++)
      if (sys->services[j].code == code)
        return false;
    sys->services[i] = (nfc_felica_service_t){code, kv};
  }
  for (size_t i = 0; i < sys->area_count; i++) {
    if (sys->areas[i].first > sys->service_count ||
        (sys->service_count && sys->areas[i].last >= sys->service_count))
      return false;
  }
  snprintf(key, sizeof(key), "%s Public blocks read", prefix);
  if (!number(doc, key, &sys->block_count, NFC_FELICA_BLOCKS - *total))
    return false;
  *total += sys->block_count;
  sys->blocks = allocate(sys->block_count, sizeof(*sys->blocks));
  if (sys->block_count && !sys->blocks)
    return false;
  for (size_t i = 0; i < sys->block_count; i++) {
    snprintf(key, sizeof(key), "%s Block %04X", prefix, (unsigned)i);
    value = get(doc, key);
    unsigned block = 0;
    end = 0;
    if (!value ||
        sscanf(value, "| Service code %4X | Block index %2X | Data: %n", &code, &block, &end) !=
            2 ||
        !end)
      return false;
    nfc_felica_public_t *entry = &sys->blocks[i];
    entry->service = code;
    entry->index = block;
    if (!bytes(value + end, entry->block.data, NFC_FELICA_BYTES, true))
      return false;
    bool has_service = false;
    for (size_t j = 0; j < sys->service_count; j++)
      if (sys->services[j].code == code)
        has_service = true;
    if (!has_service)
      return false;
    for (size_t j = 0; j < i; j++)
      if (sys->blocks[j].service == code && sys->blocks[j].index == block)
        return false;
  }
  return true;
}

esp_err_t nfc_felica_load(const char *path, nfc_felica_card_t *out) {
  if (!path || !out || out->systems)
    return ESP_ERR_INVALID_ARG;
  nfc_felica_document_t doc = {0};
  nfc_felica_card_t card = {0};
  FILE *file = fopen(path, "rb");
  if (!file)
    return ESP_ERR_NOT_FOUND;
  esp_err_t error = ESP_ERR_INVALID_RESPONSE;
  if (fseek(file, 0, SEEK_END))
    goto cleanup;
  long size = ftell(file);
  if (size <= 0 || size >= TEXT_LIMIT || fseek(file, 0, SEEK_SET))
    goto cleanup;
  doc.text = allocate((size_t)size + 1, 1);
  if (!doc.text) {
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  if (fread(doc.text, 1, size, file) != (size_t)size)
    goto cleanup;
  for (long i = 0; i < size; i++) {
    unsigned char c = doc.text[i];
    if (!c || (c < ' ' && c != '\r' && c != '\n' && c != '\t'))
      goto cleanup;
    if (c == '\n')
      doc.count++;
  }
  if (++doc.count > FIELD_LIMIT)
    goto cleanup;
  doc.fields = allocate(doc.count, sizeof(*doc.fields));
  if (!doc.fields) {
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  doc.count = 0;
  char prefix[KEY_SIZE] = "";
  for (char *line = doc.text; line && *line;) {
    char *next = strchr(line, '\n');
    if (next)
      *next++ = 0;
    char *key = trim(line), *value = strchr(key, ':');
    if (*key && *key != '#' && value) {
      *value++ = 0;
      key = trim(key);
      value = trim(value);
      unsigned idx = 0;
      int end = 0;
      bool is_system = sscanf(key, "System %2X%n", &idx, &end) == 1 && end && !key[end];
      if (is_system) {
        if (idx >= NFC_FELICA_SYSTEMS)
          goto cleanup;
        snprintf(prefix, sizeof(prefix), "System %02X", idx);
      }
      if (strcmp(key, "Directory Tree")) {
        nfc_felica_field_t *field = &doc.fields[doc.count++];
        int length = snprintf(field->key,
                              sizeof(field->key),
                              "%s%s%s",
                              *prefix && !is_system ? prefix : "",
                              *prefix && !is_system ? " " : "",
                              key);
        if (length < 0 || (size_t)length >= sizeof(field->key))
          goto cleanup;
        field->value = value;
      }
    }
    line = next;
  }
  qsort(doc.fields, doc.count, sizeof(*doc.fields), compare);
  for (size_t i = 1; i < doc.count; i++)
    if (!strcmp(doc.fields[i - 1].key, doc.fields[i].key))
      goto cleanup;
  const char *type = get(&doc, "Filetype"), *device = get(&doc, "Device type");
  size_t version = 0, outer = 0;
  if (!type || strcmp(type, "Flipper NFC device") || !device || strcmp(device, "FeliCa") ||
      !number(&doc, "Version", &outer, FORMAT_VERSION) || outer != FORMAT_VERSION ||
      !number(&doc, "Data format version", &version, FORMAT_VERSION) || !version ||
      !bytes(get(&doc, "Manufacture id"), card.tag.idm, FELICA_IDM_LEN, false) ||
      !bytes(get(&doc, "Manufacture parameter"), card.tag.pmm, FELICA_PMM_LEN, false))
    goto cleanup;
  uint8_t uid[FELICA_IDM_LEN];
  if (!bytes(get(&doc, "UID"), uid, sizeof(uid), false) || memcmp(uid, card.tag.idm, sizeof(uid)))
    goto cleanup;
  card.type = version == 1 ? NFC_FELICA_TYPE_LITE : nfc_felica_type(card.tag.pmm);
  if (!flag(&doc, "Highboy key known", &card.has_key))
    goto cleanup;
  /* Authentication is session state; never trust it from a saved document. */
  if (card.type == NFC_FELICA_TYPE_LITE) {
    size_t total = 0, read = 0;
    if (!number(&doc, "Blocks total", &total, NFC_FELICA_LITE_BLOCKS) ||
        !number(&doc, "Blocks read", &read, total))
      goto cleanup;
    card.blocks_total = total;
    card.blocks_read = read;
    for (size_t i = 0; i < NFC_FELICA_LITE_BLOCKS; i++) {
      card.lite[i].sf1 = 1;
      card.lite[i].sf2 = NFC_FELICA_SF_BLOCK;
    }
    for (size_t i = 0; i < total; i++) {
      char key[KEY_SIZE];
      snprintf(key, sizeof(key), "Block %u", (unsigned)i);
      uint8_t block[NFC_FELICA_BYTES + 2];
      if (!bytes(get(&doc, key), block, sizeof(block), false))
        goto cleanup;
      card.lite[i].sf1 = block[0];
      card.lite[i].sf2 = block[1];
      memcpy(card.lite[i].data, block + 2, NFC_FELICA_BYTES);
    }
    /* Older fork files encode a supplied key through the complete-read count. */
    if (!get(&doc, "Highboy key known"))
      card.has_key = read == NFC_FELICA_LITE_BLOCKS;
    if (card.has_key && total <= (size_t)nfc_felica_lite_index(NFC_FELICA_CK))
      goto cleanup;
  } else if (card.type == NFC_FELICA_TYPE_STANDARD) {
    if (!number(&doc, "System found", &card.system_count, NFC_FELICA_SYSTEMS))
      goto cleanup;
    card.systems = allocate(card.system_count, sizeof(*card.systems));
    if (card.system_count && !card.systems) {
      error = ESP_ERR_NO_MEM;
      goto cleanup;
    }
    size_t total = 0;
    for (size_t i = 0; i < card.system_count; i++)
      if (!parse_system(&doc, &card.systems[i], i, version, &total))
        goto cleanup;
  }
  *out = card;
  memset(&card, 0, sizeof(card));
  error = ESP_OK;
cleanup:
  fclose(file);
  free(doc.fields);
  free(doc.text);
  nfc_felica_free(&card);
  return error;
}

static bool emit(nfc_felica_document_t *doc, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int length = vsnprintf(doc->text + doc->used, TEXT_LIMIT - doc->used, format, args);
  va_end(args);
  if (length < 0 || (size_t)length >= TEXT_LIMIT - doc->used)
    return false;
  doc->used += length;
  return true;
}
static bool emit_bytes(nfc_felica_document_t *doc, const uint8_t *data, size_t size) {
  for (size_t i = 0; i < size; i++)
    if (!emit(doc, "%02X%s", data[i], i + 1 == size ? "" : " "))
      return false;
  return true;
}

esp_err_t nfc_felica_save(const char *path, const nfc_felica_card_t *card) {
  if (!path || !card || card->system_count > NFC_FELICA_SYSTEMS ||
      (card->system_count && !card->systems) || card->blocks_total > NFC_FELICA_LITE_BLOCKS ||
      card->blocks_read > card->blocks_total)
    return ESP_ERR_INVALID_ARG;
  nfc_felica_document_t doc = {0};
  doc.text = allocate(TEXT_LIMIT, 1);
  if (!doc.text)
    return ESP_ERR_NO_MEM;
  esp_err_t error = ESP_ERR_INVALID_SIZE;
#define EMIT(...)                 \
  do {                            \
    if (!emit(&doc, __VA_ARGS__)) \
      goto cleanup;               \
  } while (0)
#define BYTES(data, size)              \
  do {                                 \
    if (!emit_bytes(&doc, data, size)) \
      goto cleanup;                    \
  } while (0)
  EMIT("# Copyright (c) 2025 HIGH CODE LLC\n#\n# TentacleOS is free software: you can redistribute "
       "it and/or modify\n# it under the terms of the GNU General Public License as published "
       "by\n# the Free Software Foundation, either version 3 of the License, or\n# (at your "
       "option) any later version.\n#\n# TentacleOS is distributed in the hope that it will be "
       "useful,\n# but WITHOUT ANY WARRANTY; without even the implied warranty of\n# "
       "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n# GNU General Public License "
       "for more details.\n#\n# You should have received a copy of the GNU General Public "
       "License\n# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.\n");
  EMIT("Filetype: Flipper NFC device\nVersion: 4\nDevice type: FeliCa\nUID: ");
  BYTES(card->tag.idm, FELICA_IDM_LEN);
  EMIT("\nData format version: 4\nManufacture id: ");
  BYTES(card->tag.idm, FELICA_IDM_LEN);
  EMIT("\nManufacture parameter: ");
  BYTES(card->tag.pmm, FELICA_PMM_LEN);
  EMIT("\nHighboy key known: %s\n", card->has_key ? "true" : "false");
  if (card->type == NFC_FELICA_TYPE_LITE) {
    EMIT("Blocks total: %u\nBlocks read: %u\n", card->blocks_total, card->blocks_read);
    for (size_t i = 0; i < card->blocks_total; i++) {
      EMIT("Block %u: %02X %02X ", (unsigned)i, card->lite[i].sf1, card->lite[i].sf2);
      BYTES(card->lite[i].data, NFC_FELICA_BYTES);
      EMIT("\n");
    }
  } else if (card->type == NFC_FELICA_TYPE_STANDARD) {
    EMIT("System found: %u\n", (unsigned)card->system_count);
    size_t total = 0;
    for (size_t i = 0; i < card->system_count; i++) {
      const nfc_felica_system_t *sys = &card->systems[i];
      if (sys->area_count > NFC_FELICA_NODES ||
          sys->service_count > NFC_FELICA_NODES - sys->area_count ||
          sys->block_count > NFC_FELICA_BLOCKS - total || (sys->area_count && !sys->areas) ||
          (sys->service_count && !sys->services) || (sys->block_count && !sys->blocks))
        goto cleanup;
      total += sys->block_count;
      EMIT("\nSystem %02X: %04X | Key version %04X |\nArea found: %u\n",
           (unsigned)i,
           sys->code,
           sys->key_version,
           (unsigned)sys->area_count);
      for (size_t j = 0; j < sys->area_count; j++) {
        const nfc_felica_area_t *a = &sys->areas[j];
        EMIT("Area %03X: | Code %04X | End %04X | Services #%03X-#%03X | Key version %04X |\n",
             (unsigned)j,
             a->code,
             a->end,
             a->first,
             a->last,
             a->key_version);
      }
      EMIT("Service found: %u\n", (unsigned)sys->service_count);
      for (size_t j = 0; j < sys->service_count; j++)
        EMIT("Service %03X: | Code %04X | Attrib. %02X | Key version %04X |\n",
             (unsigned)j,
             sys->services[j].code,
             sys->services[j].code & NFC_FELICA_ATTR_MASK,
             sys->services[j].key_version);
      EMIT("Public blocks read: %u\n", (unsigned)sys->block_count);
      for (size_t j = 0; j < sys->block_count; j++) {
        const nfc_felica_public_t *b = &sys->blocks[j];
        EMIT("Block %04X: | Service code %04X | Block index %02X | Data: ",
             (unsigned)j,
             b->service,
             b->index);
        BYTES(b->block.data, NFC_FELICA_BYTES);
        EMIT(" |\n");
      }
    }
  }
  error = storage_write_atomic(path, doc.text, doc.used);
cleanup:
  free(doc.text);
  return error;
#undef EMIT
#undef BYTES
}
