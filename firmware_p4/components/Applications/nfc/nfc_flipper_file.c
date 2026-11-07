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

#include "nfc_flipper_file.h"

#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#define LINE_CAPACITY       192
#define FILE_LINE_LIMIT     512
#define HEADER_MASK         ((1U << NFC_FLIPPER_HEADER_COUNT) - 1U)
#define UID_SINGLE_SIZE     4
#define UID_DOUBLE_SIZE     7
#define HEX_ALPHA_OFFSET    10
#define HEX_SHIFT           4
#define FILE_MODE           0600
#define BLOCK_PREFIX        "Block "
#define KEY_SIZE            6
#define KEY_B_OFFSET        10
#define SMALL_SECTOR_BLOCKS 4
#define LARGE_SECTOR_BLOCKS 16
#define SMALL_SECTORS       32
#define MINI_BLOCKS         20
#define CLASSIC_1K_BLOCKS   64
#define CLASSIC_4K_BLOCKS   256

typedef enum {
  NFC_FLIPPER_HEADER_FILETYPE,
  NFC_FLIPPER_HEADER_VERSION,
  NFC_FLIPPER_HEADER_DEVICE,
  NFC_FLIPPER_HEADER_UID,
  NFC_FLIPPER_HEADER_ATQA,
  NFC_FLIPPER_HEADER_SAK,
  NFC_FLIPPER_HEADER_CLASSIC_TYPE,
  NFC_FLIPPER_HEADER_DATA_VERSION,
  NFC_FLIPPER_HEADER_COUNT
} nfc_flipper_header_t;

static const char *TAG = "NFC_FLIPPER_FILE";

static char *trim(char *text) {
  while (isspace((unsigned char)*text))
    text++;
  size_t size = strlen(text);
  while (size && isspace((unsigned char)text[size - 1]))
    text[--size] = '\0';
  return text;
}

static int hex_digit(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  c = (char)tolower((unsigned char)c);
  return c >= 'a' && c <= 'f' ? c - 'a' + HEX_ALPHA_OFFSET : -1;
}

static size_t parse_hex(const char *text, uint8_t *out, size_t capacity) {
  size_t count = 0;
  while (*text) {
    while (isspace((unsigned char)*text))
      text++;
    if (!*text)
      break;
    if (!text[1] || count == capacity)
      return 0;
    int high = hex_digit(text[0]);
    int low = hex_digit(text[1]);
    if (high < 0 || low < 0 || (text[2] && !isspace((unsigned char)text[2])))
      return 0;
    out[count++] = (uint8_t)((high << HEX_SHIFT) | low);
    text += 2;
  }
  return count;
}

esp_err_t nfc_flipper_file_load_classic(const char *path, mfc_emu_card_data_t *out_card) {
  if (!out_card)
    return ESP_ERR_INVALID_ARG;
  memset(out_card, 0, sizeof(*out_card));
  if (!path)
    return ESP_ERR_INVALID_ARG;
  FILE *file = fopen(path, "r");
  if (!file)
    return ESP_ERR_NOT_FOUND;
  esp_err_t err = ESP_ERR_INVALID_RESPONSE;
  char line[LINE_CAPACITY];
  bool seen[MFC_EMU_MAX_BLOCKS] = {false};
  unsigned headers = 0;
  unsigned lines = 0;
  unsigned blocks = 0;
  const char *names[NFC_FLIPPER_HEADER_COUNT] = {"Filetype",
                                                 "Version",
                                                 "Device type",
                                                 "UID",
                                                 "ATQA",
                                                 "SAK",
                                                 "Mifare Classic type",
                                                 "Data format version"};
  while (fgets(line, sizeof(line), file)) {
    if (++lines > FILE_LINE_LIMIT || (!strchr(line, '\n') && !feof(file)))
      goto cleanup;
    char *key = trim(line);
    if (!*key || *key == '#')
      continue;
    char *separator = strchr(key, ':');
    if (!separator)
      goto cleanup;
    *separator = '\0';
    key = trim(key);
    char *value = trim(separator + 1);
    unsigned index = 0;
    while (index < NFC_FLIPPER_HEADER_COUNT && strcmp(key, names[index]))
      index++;
    if (index < NFC_FLIPPER_HEADER_COUNT) {
      if (headers & (1U << index))
        goto cleanup;
      headers |= 1U << index;
      switch (index) {
        case NFC_FLIPPER_HEADER_FILETYPE:
          if (strcmp(value, "Flipper NFC device"))
            goto cleanup;
          break;
        case NFC_FLIPPER_HEADER_VERSION:
          if (strcmp(value, "3") && strcmp(value, "4"))
            goto cleanup;
          break;
        case NFC_FLIPPER_HEADER_DEVICE:
          if (strcmp(value, "Mifare Classic"))
            goto cleanup;
          break;
        case NFC_FLIPPER_HEADER_UID:
          out_card->uid_len = parse_hex(value, out_card->uid, sizeof(out_card->uid));
          if (out_card->uid_len != UID_SINGLE_SIZE && out_card->uid_len != UID_DOUBLE_SIZE)
            goto cleanup;
          break;
        case NFC_FLIPPER_HEADER_ATQA: {
          uint8_t atqa[2];
          if (parse_hex(value, atqa, sizeof(atqa)) != sizeof(atqa))
            goto cleanup;
          out_card->atqa[0] = atqa[1];
          out_card->atqa[1] = atqa[0];
          break;
        }
        case NFC_FLIPPER_HEADER_SAK:
          if (parse_hex(value, &out_card->sak, 1) != 1)
            goto cleanup;
          break;
        case NFC_FLIPPER_HEADER_CLASSIC_TYPE:
          if (!strcmp(value, "MINI")) {
            out_card->type = MF_CLASSIC_MINI;
            out_card->total_blocks = MINI_BLOCKS;
          } else if (!strcmp(value, "1K")) {
            out_card->type = MF_CLASSIC_1K;
            out_card->total_blocks = CLASSIC_1K_BLOCKS;
          } else if (!strcmp(value, "4K")) {
            out_card->type = MF_CLASSIC_4K;
            out_card->total_blocks = CLASSIC_4K_BLOCKS;
          } else
            goto cleanup;
          break;
        case NFC_FLIPPER_HEADER_DATA_VERSION:
          if (strcmp(value, "2"))
            goto cleanup;
          break;
      }
    } else if (!strncmp(key, BLOCK_PREFIX, sizeof(BLOCK_PREFIX) - 1)) {
      const char *number = key + sizeof(BLOCK_PREFIX) - 1;
      if (!isdigit((unsigned char)*number))
        goto cleanup;
      char *end;
      unsigned long block = strtoul(number, &end, 10);
      if (*end || block >= MFC_EMU_MAX_BLOCKS || seen[block])
        goto cleanup;
      if (parse_hex(value, out_card->blocks[block], MFC_EMU_BLOCK_SIZE) != MFC_EMU_BLOCK_SIZE)
        goto cleanup;
      seen[block] = true;
      blocks++;
    } else {
      goto cleanup;
    }
  }
  if (ferror(file) || headers != HEADER_MASK || blocks != (unsigned)out_card->total_blocks)
    goto cleanup;
  for (int block = 0; block < out_card->total_blocks; block++)
    if (!seen[block])
      goto cleanup;
  for (int block = 0; block < out_card->total_blocks;) {
    int size = out_card->sector_count < SMALL_SECTORS ? SMALL_SECTOR_BLOCKS : LARGE_SECTOR_BLOCKS;
    const uint8_t *trailer = out_card->blocks[block + size - 1];
    int sector = out_card->sector_count++;
    memcpy(out_card->keys[sector].key_a, trailer, KEY_SIZE);
    memcpy(out_card->keys[sector].key_b, trailer + KEY_B_OFFSET, KEY_SIZE);
    out_card->keys[sector].key_a_known = true;
    out_card->keys[sector].key_b_known = true;
    block += size;
  }
  err = ESP_OK;
cleanup:
  if (fclose(file) != 0 && err == ESP_OK)
    err = ESP_FAIL;
  if (err != ESP_OK) {
    memset(out_card, 0, sizeof(*out_card));
    ESP_LOGW(TAG, "Incomplete or unsupported Classic NFC file: %s", path);
  }
  return err;
}

esp_err_t nfc_flipper_file_save_classic(const char *path, const mfc_emu_card_data_t *card) {
  if (!path || !card || (card->uid_len != UID_SINGLE_SIZE && card->uid_len != UID_DOUBLE_SIZE))
    return ESP_ERR_INVALID_ARG;
  const char *type;
  int blocks;
  switch (card->type) {
    case MF_CLASSIC_MINI:
      type = "MINI";
      blocks = MINI_BLOCKS;
      break;
    case MF_CLASSIC_1K:
      type = "1K";
      blocks = CLASSIC_1K_BLOCKS;
      break;
    case MF_CLASSIC_4K:
      type = "4K";
      blocks = CLASSIC_4K_BLOCKS;
      break;
    default:
      return ESP_ERR_INVALID_ARG;
  }
  int sectors =
      blocks <= SMALL_SECTORS * SMALL_SECTOR_BLOCKS
          ? blocks / SMALL_SECTOR_BLOCKS
          : SMALL_SECTORS + (blocks - SMALL_SECTORS * SMALL_SECTOR_BLOCKS) / LARGE_SECTOR_BLOCKS;
  if (card->total_blocks != blocks || card->sector_count != sectors)
    return ESP_ERR_INVALID_ARG;
  for (int sector = 0; sector < sectors; sector++)
    if (!card->keys[sector].key_a_known || !card->keys[sector].key_b_known)
      return ESP_ERR_INVALID_ARG;
  int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, FILE_MODE);
  if (fd < 0)
    return ESP_FAIL;
  FILE *file = fdopen(fd, "w");
  if (!file) {
    close(fd);
    remove(path);
    return ESP_FAIL;
  }
  fprintf(file, "Filetype: Flipper NFC device\nVersion: 4\nDevice type: Mifare Classic\nUID:");
  for (uint8_t i = 0; i < card->uid_len; i++)
    fprintf(file, " %02X", card->uid[i]);
  fprintf(file,
          "\nATQA: %02X %02X\nSAK: %02X\nMifare Classic type: %s\n"
          "Data format version: 2\n",
          card->atqa[1],
          card->atqa[0],
          card->sak,
          type);
  int sector = 0;
  int trailer = SMALL_SECTOR_BLOCKS - 1;
  for (int block = 0; block < blocks; block++) {
    uint8_t data[MFC_EMU_BLOCK_SIZE];
    memcpy(data, card->blocks[block], sizeof(data));
    if (block == trailer) {
      memcpy(data, card->keys[sector].key_a, KEY_SIZE);
      memcpy(data + KEY_B_OFFSET, card->keys[sector].key_b, KEY_SIZE);
      sector++;
      trailer += sector < SMALL_SECTORS ? SMALL_SECTOR_BLOCKS : LARGE_SECTOR_BLOCKS;
    }
    fprintf(file, "Block %d:", block);
    for (size_t i = 0; i < sizeof(data); i++)
      fprintf(file, " %02X", data[i]);
    fputc('\n', file);
  }
  bool failed = ferror(file) != 0;
  if (fclose(file) != 0)
    failed = true;
  if (failed) {
    remove(path);
    ESP_LOGW(TAG, "Could not save Classic NFC file: %s", path);
    return ESP_FAIL;
  }
  return ESP_OK;
}
