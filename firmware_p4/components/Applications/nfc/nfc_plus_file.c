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

#include "nfc_plus_file.h"

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "storage_atomic.h"

#define LINE_CAPACITY             256U
#define LINE_LIMIT                512U
#define HEX_SHIFT                 4U
#define HEX_ALPHA                 10U
#define KEY_BASE                  0x4000U
#define CONFIG_BASE               0xB000U
#define CONFIG_COUNT              4U
#define ADMIN_COUNT               4U
#define SMALL_BLOCK_LIMIT         128U
#define SMALL_SECTOR_BLOCKS       4U
#define LARGE_SECTOR_BLOCKS       16U
#define LARGE_SECTOR_BASE         32U
#define REQUIRED_HEADERS          0x3FFU
#define FIELD_COUNT               18U
#define MEMORY_1K                 64U
#define MEMORY_2K                 128U
#define ATS_LENGTH                3U
#define ATS_T0                    0x46U
#define ATS_TC1                   0x02U
#define TEXT_CAPACITY             32768U
#define ACCESS_OFFSET             6U
#define ACCESS_NIBBLE             4U
#define ACCESS_MASK               0x0FU
#define LARGE_ACCESS_GROUP_BLOCKS 5U
#define TRAILER_GROUP             3U
#define KEY_A_MASK                1U
#define KEY_B_MASK                2U
#define UID_SHORT_SIZE            4U
#define UID_LONG_SIZE             7U

static const char *TAG = "NFC_PLUS_FILE";

static char *trim(char *s) {
  while (isspace((unsigned char)*s))
    s++;
  size_t n = strlen(s);
  while (n && isspace((unsigned char)s[n - 1]))
    s[--n] = 0;
  return s;
}

static int digit(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  c = (char)tolower((unsigned char)c);
  return c >= 'a' && c <= 'f' ? c - 'a' + HEX_ALPHA : -1;
}

static bool bytes(const char *s, uint8_t *out, size_t size, bool *out_known) {
  bool known = true, unknown = false;
  for (size_t i = 0; i < size; i++) {
    while (isspace((unsigned char)*s))
      s++;
    if (!s[0] || !s[1])
      return false;
    if (s[0] == '?' && s[1] == '?') {
      unknown = true;
      out[i] = 0;
    } else {
      int a = digit(s[0]), b = digit(s[1]);
      if (a < 0 || b < 0)
        return false;
      out[i] = (uint8_t)((a << HEX_SHIFT) | b);
      known = false;
    }
    s += 2;
    if (*s && !isspace((unsigned char)*s))
      return false;
  }
  while (isspace((unsigned char)*s))
    s++;
  if (*s || (unknown && !known) || (unknown && out_known == NULL))
    return false;
  if (out_known)
    *out_known = !unknown;
  return true;
}

static bool indexed(const char *text, const char *prefix, size_t limit, size_t *out_index) {
  size_t n = strlen(prefix);
  if (strncmp(text, prefix, n) || !isdigit((unsigned char)text[n]))
    return false;
  char *end;
  unsigned long value = strtoul(text + n, &end, 10);
  if (*end || value >= limit)
    return false;
  *out_index = value;
  return true;
}

esp_err_t nfc_plus_file_load(const char *path, nfc_plus_file_t *out_card) {
  if (out_card == NULL)
    return ESP_ERR_INVALID_ARG;
  memset(out_card, 0, sizeof(*out_card));
  if (path == NULL)
    return ESP_ERR_INVALID_ARG;
  FILE *file = fopen(path, "r");
  if (file == NULL)
    return ESP_ERR_NOT_FOUND;
  esp_err_t err = ESP_ERR_INVALID_RESPONSE;
  const char *fields[FIELD_COUNT] = {"Filetype",
                                     "Version",
                                     "Device type",
                                     "UID",
                                     "ATQA",
                                     "SAK",
                                     "PICC Version",
                                     "Security Level",
                                     "Memory Size",
                                     "Data format version",
                                     "Card Type",
                                     "T0",
                                     "TA(1)",
                                     "TB(1)",
                                     "TC(1)",
                                     "T1...Tk",
                                     "Signature",
                                     "Reserved"};
  uint32_t seen_fields = 0;
  bool seen_blocks[NFC_PLUS_FILE_MAX_BLOCKS] = {false};
  bool seen_keys[NFC_PLUS_FILE_MAX_KEYS] = {false};
  bool seen_extra[8] = {false};
  char line[LINE_CAPACITY];
  unsigned lines = 0;
  while (fgets(line, sizeof(line), file)) {
    if (++lines > LINE_LIMIT || (!strchr(line, '\n') && !feof(file)))
      goto cleanup;
    char *name = trim(line);
    if (!*name || *name == '#')
      continue;
    char *separator = strchr(name, ':');
    if (separator == NULL)
      goto cleanup;
    *separator = 0;
    name = trim(name);
    char *value = trim(separator + 1);
    unsigned field = 0;
    while (field < FIELD_COUNT && strcmp(name, fields[field]))
      field++;
    if (field < FIELD_COUNT) {
      if (seen_fields & (1UL << field))
        goto cleanup;
      seen_fields |= 1UL << field;
      switch (field) {
        case 0:
          if (strcmp(value, "Flipper NFC device"))
            goto cleanup;
          break;
        case 1:
          if (strcmp(value, "4"))
            goto cleanup;
          break;
        case 2:
          if (strcmp(value, "Mifare Plus"))
            goto cleanup;
          break;
        case 3:
          if (bytes(value, out_card->identity.uid, 4, NULL))
            out_card->identity.uid_len = 4;
          else if (bytes(value, out_card->identity.uid, 7, NULL))
            out_card->identity.uid_len = 7;
          else
            goto cleanup;
          break;
        case 4: {
          uint8_t atqa[2];
          if (!bytes(value, atqa, sizeof(atqa), NULL))
            goto cleanup;
          out_card->identity.atqa[0] = atqa[1];
          out_card->identity.atqa[1] = atqa[0];
          break;
        }
        case 5:
          if (!bytes(value, &out_card->identity.sak, 1, NULL) || !(out_card->identity.sak & 0x20))
            goto cleanup;
          break;
        case 6:
          if (!bytes(value, out_card->version, sizeof(out_card->version), NULL))
            goto cleanup;
          break;
        case 7:
          if (strcmp(value, "SL3"))
            goto cleanup;
          break;
        case 8:
          if (!strcmp(value, "1K"))
            out_card->block_count = MEMORY_1K;
          else if (!strcmp(value, "2K"))
            out_card->block_count = MEMORY_2K;
          else if (!strcmp(value, "4K"))
            out_card->block_count = NFC_PLUS_FILE_MAX_BLOCKS;
          else
            goto cleanup;
          break;
        case 9:
          if (strcmp(value, "1"))
            goto cleanup;
          break;
        case 10:
          if (strncmp(value, "Mifare Plus", strlen("Mifare Plus")) ||
              strlen(value) >= sizeof(out_card->card_type))
            goto cleanup;
          memcpy(out_card->card_type, value, strlen(value) + 1);
          break;
        case 11:
        case 12:
        case 13:
        case 14: {
          uint8_t byte;
          if (!bytes(value, &byte, 1, NULL))
            goto cleanup;
          break;
        }
        case 15: {
          uint8_t historical[T4T_EMU_ATS_CAPACITY];
          size_t n = (*value ? (strlen(value) + 1) / 3 : 0);
          if (n > sizeof(historical) || (n && !bytes(value, historical, n, NULL)))
            goto cleanup;
          break;
        }
        case 16:
          if (!bytes(value, out_card->signature, sizeof(out_card->signature), NULL))
            goto cleanup;
          out_card->has_signature = true;
          break;
        default:
          goto cleanup;
      }
      continue;
    }
    size_t index;
    if (indexed(name, "Block ", NFC_PLUS_FILE_MAX_BLOCKS, &index)) {
      if (seen_blocks[index] ||
          !bytes(value, out_card->blocks[index], MFP_BLOCK_SIZE, &out_card->has_block[index]))
        goto cleanup;
      seen_blocks[index] = true;
    } else if (indexed(name, "Key A ", NFC_PLUS_FILE_MAX_KEYS / 2, &index) ||
               indexed(name, "Key B ", NFC_PLUS_FILE_MAX_KEYS / 2, &index)) {
      index = index * 2 + (name[4] == 'B');
      if (seen_keys[index] ||
          !bytes(value, out_card->keys[index], MFP_BLOCK_SIZE, &out_card->has_key[index]))
        goto cleanup;
      seen_keys[index] = true;
    } else {
      const char *extra[] = {
          "Card Master Key", "Card Config Key", "L3 Switch Key", "SL1 Card Auth Key"};
      unsigned i = 0;
      while (i < 4 && strcmp(name, extra[i]))
        i++;
      if (i == 4) {
        if (!indexed(name, "Config Block ", 4, &index))
          goto cleanup;
        i += index;
      }
      if (seen_extra[i] ||
          !bytes(value, out_card->extra[i], MFP_BLOCK_SIZE, &out_card->has_extra[i]))
        goto cleanup;
      seen_extra[i] = true;
      out_card->extra_present[i] = true;
    }
  }
  if (ferror(file) || (seen_fields & REQUIRED_HEADERS) != REQUIRED_HEADERS)
    goto cleanup;
  unsigned sectors =
      out_card->block_count <= SMALL_BLOCK_LIMIT
          ? out_card->block_count / SMALL_SECTOR_BLOCKS
          : LARGE_SECTOR_BASE + (out_card->block_count - SMALL_BLOCK_LIMIT) / LARGE_SECTOR_BLOCKS;
  for (size_t i = 0; i < NFC_PLUS_FILE_MAX_BLOCKS; i++)
    if (seen_blocks[i] != (i < out_card->block_count))
      goto cleanup;
  for (size_t i = 0; i < NFC_PLUS_FILE_MAX_KEYS; i++)
    if (seen_keys[i] != (i < sectors * 2U))
      goto cleanup;
  /* Advertise our 106 kbit/s transport rather than unsupported captured rates. */
  out_card->identity.ats_len = ATS_LENGTH;
  out_card->identity.ats[0] = ATS_LENGTH;
  out_card->identity.ats[1] = ATS_T0;
  out_card->identity.ats[2] = ATS_TC1;
  err = ESP_OK;
cleanup:
  if (fclose(file) != 0 && err == ESP_OK)
    err = ESP_FAIL;
  if (err != ESP_OK) {
    memset(out_card, 0, sizeof(*out_card));
    ESP_LOGD(TAG, "Unsupported or incomplete Plus dump: %s", path);
  }
  return err;
}

static unsigned sector_count(const nfc_plus_file_t *card) {
  return card->block_count <= SMALL_BLOCK_LIMIT
             ? card->block_count / SMALL_SECTOR_BLOCKS
             : LARGE_SECTOR_BASE + (card->block_count - SMALL_BLOCK_LIMIT) / LARGE_SECTOR_BLOCKS;
}

static int admin_index(uint16_t address) {
  static const uint16_t ADDRESSES[ADMIN_COUNT] = {0x9000U, 0x9001U, 0x9003U, 0x9004U};
  for (unsigned i = 0; i < ADMIN_COUNT; i++)
    if (address == ADDRESSES[i])
      return (int)i;
  return -1;
}

static bool resolve_key(void *context, uint16_t address, uint8_t out_key[MFP_BLOCK_SIZE]) {
  nfc_plus_file_t *card = context;
  int admin = admin_index(address);
  if (admin >= 0) {
    if (!card->has_extra[admin])
      return false;
    memcpy(out_key, card->extra[admin], MFP_BLOCK_SIZE);
    return true;
  }
  if (address < KEY_BASE || address - KEY_BASE >= sector_count(card) * 2U)
    return false;
  size_t index = address - KEY_BASE;
  if (!card->has_key[index])
    return false;
  memcpy(out_key, card->keys[index], MFP_BLOCK_SIZE);
  return true;
}

static bool access_condition(const uint8_t *block, unsigned group, uint8_t *out_condition) {
  const uint8_t *access = block + ACCESS_OFFSET;
  uint8_t c1 = access[1] >> ACCESS_NIBBLE;
  uint8_t c2 = access[2] & ACCESS_MASK;
  uint8_t c3 = access[2] >> ACCESS_NIBBLE;
  if (((access[0] ^ c1) & ACCESS_MASK) != ACCESS_MASK ||
      (((access[0] >> ACCESS_NIBBLE) ^ c2) & ACCESS_MASK) != ACCESS_MASK ||
      ((access[1] ^ c3) & ACCESS_MASK) != ACCESS_MASK)
    return false;
  *out_condition = (((c1 >> group) & 1U) << 2) | (((c2 >> group) & 1U) << 1) | ((c3 >> group) & 1U);
  return true;
}

static bool sector_access(const nfc_plus_file_t *card,
                          uint16_t key,
                          uint16_t address,
                          uint8_t *out_condition,
                          bool *out_is_trailer) {
  if (address >= card->block_count || key < KEY_BASE || key - KEY_BASE >= sector_count(card) * 2U ||
      !card->has_key[key - KEY_BASE])
    return false;
  unsigned sector = address < SMALL_BLOCK_LIMIT
                        ? address / SMALL_SECTOR_BLOCKS
                        : LARGE_SECTOR_BASE + (address - SMALL_BLOCK_LIMIT) / LARGE_SECTOR_BLOCKS;
  if ((key - KEY_BASE) / 2U != sector)
    return false;
  unsigned sector_size = address < SMALL_BLOCK_LIMIT ? SMALL_SECTOR_BLOCKS : LARGE_SECTOR_BLOCKS;
  unsigned trailer = address - address % sector_size + sector_size - 1;
  if (!card->has_block[trailer])
    return false;
  *out_is_trailer = address == trailer;
  unsigned group = address % sector_size;
  if (sector_size == LARGE_SECTOR_BLOCKS)
    group /= LARGE_ACCESS_GROUP_BLOCKS;
  return access_condition(card->blocks[trailer], group, out_condition);
}

static bool can_access(const nfc_plus_file_t *card, uint16_t key, uint16_t address, bool is_write) {
  uint8_t condition;
  bool is_trailer;
  if (!sector_access(card, key, address, &condition, &is_trailer))
    return false;
  static const uint8_t READ_MASK[] = {3, 3, 3, 2, 3, 2, 3, 0};
  static const uint8_t WRITE_MASK[] = {3, 0, 0, 2, 2, 0, 2, 0};
  static const uint8_t TRAILER_READ_MASK[] = {1, 1, 1, 3, 3, 3, 3, 3};
  static const uint8_t TRAILER_WRITE_MASK[] = {0, 1, 0, 2, 0, 2, 0, 0};
  uint8_t key_mask = (key - KEY_BASE) & 1U ? KEY_B_MASK : KEY_A_MASK;
  if (is_write && address == 0)
    return false;
  const uint8_t *mask = is_trailer ? (is_write ? TRAILER_WRITE_MASK : TRAILER_READ_MASK)
                                   : (is_write ? WRITE_MASK : READ_MASK);
  return (mask[condition] & key_mask) != 0;
}

static bool replace_key(nfc_plus_file_t *card,
                        uint16_t key,
                        uint16_t address,
                        const uint8_t data[MFP_BLOCK_SIZE]) {
  if (address < KEY_BASE || address - KEY_BASE >= sector_count(card) * 2U)
    return false;
  unsigned sector = (address - KEY_BASE) / 2U;
  unsigned trailer =
      sector < LARGE_SECTOR_BASE
          ? (sector + 1) * SMALL_SECTOR_BLOCKS - 1
          : SMALL_BLOCK_LIMIT + (sector - LARGE_SECTOR_BASE + 1) * LARGE_SECTOR_BLOCKS - 1;
  uint8_t condition;
  bool is_trailer;
  if (!sector_access(card, key, trailer, &condition, &is_trailer))
    return false;
  static const uint8_t KEY_WRITE_MASK[] = {1, 1, 0, 2, 2, 0, 0, 0};
  uint8_t key_mask = (key - KEY_BASE) & 1U ? KEY_B_MASK : KEY_A_MASK;
  if (!(KEY_WRITE_MASK[condition] & key_mask))
    return false;
  memcpy(card->keys[address - KEY_BASE], data, MFP_BLOCK_SIZE);
  card->has_key[address - KEY_BASE] = true;
  return true;
}

static bool
resolve_block(void *context, uint16_t key, uint16_t address, uint8_t out[MFP_BLOCK_SIZE]) {
  nfc_plus_file_t *card = context;
  if (address >= CONFIG_BASE && address - CONFIG_BASE < CONFIG_COUNT) {
    unsigned index = ADMIN_COUNT + address - CONFIG_BASE;
    uint8_t auth_key[MFP_BLOCK_SIZE];
    if (!resolve_key(context, key, auth_key) || !card->has_extra[index])
      return false;
    memcpy(out, card->extra[index], MFP_BLOCK_SIZE);
    return true;
  }
  if (!can_access(card, key, address, false) || !card->has_block[address])
    return false;
  memcpy(out, card->blocks[address], MFP_BLOCK_SIZE);
  unsigned sector_size = address < SMALL_BLOCK_LIMIT ? SMALL_SECTOR_BLOCKS : LARGE_SECTOR_BLOCKS;
  if (address % sector_size == sector_size - 1) {
    memset(out, 0, ACCESS_OFFSET);
    memset(out + ACCESS_OFFSET + SMALL_SECTOR_BLOCKS, 0, ACCESS_OFFSET);
  }
  return true;
}

static bool
write_block(void *context, uint16_t key, uint16_t address, const uint8_t data[MFP_BLOCK_SIZE]) {
  nfc_plus_file_t *card = context;
  int admin = admin_index(address);
  bool is_config = address >= CONFIG_BASE && address - CONFIG_BASE < CONFIG_COUNT;
  if (admin >= 0 || is_config) {
    uint8_t auth_key[MFP_BLOCK_SIZE];
    if (!resolve_key(context, key, auth_key))
      return false;
    unsigned index = is_config ? ADMIN_COUNT + address - CONFIG_BASE : (unsigned)admin;
    memcpy(card->extra[index], data, MFP_BLOCK_SIZE);
    card->has_extra[index] = true;
    card->extra_present[index] = true;
    return true;
  }
  if (address >= KEY_BASE)
    return replace_key(card, key, address, data);
  if (!can_access(card, key, address, true))
    return false;
  unsigned sector_size = address < SMALL_BLOCK_LIMIT ? SMALL_SECTOR_BLOCKS : LARGE_SECTOR_BLOCKS;
  if (address % sector_size == sector_size - 1) {
    uint8_t condition;
    if (!access_condition(data, TRAILER_GROUP, &condition))
      return false;
    /* AES keys live at 0x40xx; a trailer write must not replace their placeholders. */
    memcpy(card->blocks[address] + ACCESS_OFFSET, data + ACCESS_OFFSET, SMALL_SECTOR_BLOCKS);
    return true;
  }
  memcpy(card->blocks[address], data, MFP_BLOCK_SIZE);
  card->has_block[address] = true;
  return true;
}

void nfc_plus_file_listener(nfc_plus_file_t *card, mfp_listener_config_t *out_config) {
  if (out_config == NULL)
    return;
  memset(out_config, 0, sizeof(*out_config));
  if (card == NULL)
    return;
  out_config->context = card;
  out_config->key = resolve_key;
  out_config->read = resolve_block;
  if (card->version[0] == 0x04U && (card->version[1] & 0x0FU) == 0x02U)
    out_config->version = card->version;
  out_config->signature = card->has_signature ? card->signature : NULL;
}

void nfc_plus_file_listener_mutable(nfc_plus_file_t *card, mfp_listener_config_t *out_config) {
  nfc_plus_file_listener(card, out_config);
  if (card != NULL && out_config != NULL)
    out_config->write = write_block;
}

static bool append(char *text, size_t *offset, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int count = vsnprintf(text + *offset, TEXT_CAPACITY - *offset, format, args);
  va_end(args);
  if (count < 0 || (size_t)count >= TEXT_CAPACITY - *offset)
    return false;
  *offset += (size_t)count;
  return true;
}

static bool
append_bytes(char *text, size_t *offset, const uint8_t *data, size_t length, bool known) {
  for (size_t i = 0; i < length; i++) {
    if (known) {
      if (!append(text, offset, " %02X", data[i]))
        return false;
    } else if (!append(text, offset, " ??"))
      return false;
  }
  return append(text, offset, "\n");
}

esp_err_t nfc_plus_file_save(const char *path, const nfc_plus_file_t *card) {
  if (path == NULL || card == NULL ||
      (card->block_count != MEMORY_1K && card->block_count != MEMORY_2K &&
       card->block_count != NFC_PLUS_FILE_MAX_BLOCKS) ||
      (card->identity.uid_len != UID_SHORT_SIZE && card->identity.uid_len != UID_LONG_SIZE) ||
      !(card->identity.sak & 0x20U))
    return ESP_ERR_INVALID_ARG;
  if (memchr(card->card_type, 0, sizeof(card->card_type)) == NULL ||
      strchr(card->card_type, '\n') || strchr(card->card_type, '\r') ||
      (card->card_type[0] && strncmp(card->card_type, "Mifare Plus", strlen("Mifare Plus"))))
    return ESP_ERR_INVALID_ARG;
  esp_err_t error = ESP_ERR_NO_MEM;
  char *text = malloc(TEXT_CAPACITY);
  if (text == NULL) {
    ESP_LOGE(TAG, "No memory for Plus export");
    goto cleanup;
  }
  size_t offset = 0;
  error = ESP_ERR_INVALID_SIZE;
  if (!append(text,
              &offset,
              "Filetype: Flipper NFC device\nVersion: 4\nDevice type: Mifare Plus\nUID:") ||
      !append_bytes(text, &offset, card->identity.uid, card->identity.uid_len, true) ||
      !append(text,
              &offset,
              "ATQA: %02X %02X\nSAK: %02X\nSecurity Level: SL3\nMemory Size: %s\nData format "
              "version: 1\nCard Type: %s\nPICC Version:",
              card->identity.atqa[1],
              card->identity.atqa[0],
              card->identity.sak,
              card->block_count == MEMORY_1K   ? "1K"
              : card->block_count == MEMORY_2K ? "2K"
                                               : "4K",
              card->card_type[0] ? card->card_type : "Mifare Plus") ||
      !append_bytes(text, &offset, card->version, sizeof(card->version), true))
    goto cleanup;
  if (card->has_signature &&
      (!append(text, &offset, "Signature:") ||
       !append_bytes(text, &offset, card->signature, sizeof(card->signature), true)))
    goto cleanup;
  for (unsigned i = 0; i < card->block_count; i++)
    if (!append(text, &offset, "Block %u:", i) ||
        !append_bytes(text, &offset, card->blocks[i], MFP_BLOCK_SIZE, card->has_block[i]))
      goto cleanup;
  unsigned sectors =
      card->block_count <= SMALL_BLOCK_LIMIT
          ? card->block_count / SMALL_SECTOR_BLOCKS
          : LARGE_SECTOR_BASE + (card->block_count - SMALL_BLOCK_LIMIT) / LARGE_SECTOR_BLOCKS;
  for (unsigned i = 0; i < sectors * 2U; i++)
    if (!append(text, &offset, "Key %c %u:", i & 1U ? 'B' : 'A', i / 2U) ||
        !append_bytes(text, &offset, card->keys[i], MFP_BLOCK_SIZE, card->has_key[i]))
      goto cleanup;
  static const char *extra[] = {"Card Master Key",
                                "Card Config Key",
                                "L3 Switch Key",
                                "SL1 Card Auth Key",
                                "Config Block 0",
                                "Config Block 1",
                                "Config Block 2",
                                "Config Block 3"};
  for (unsigned i = 0; i < NFC_PLUS_FILE_EXTRA_COUNT; i++)
    if (card->extra_present[i] &&
        (!append(text, &offset, "%s:", extra[i]) ||
         !append_bytes(text, &offset, card->extra[i], MFP_BLOCK_SIZE, card->has_extra[i])))
      goto cleanup;
  error = storage_write_atomic(path, text, offset);
cleanup:
  free(text);
  return error;
}
