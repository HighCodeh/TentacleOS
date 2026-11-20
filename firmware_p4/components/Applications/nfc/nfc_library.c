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

#include "nfc_library.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "vfs_sdcard.h"

#define LIBRARY_MAX_FILES      128U
#define COMPANION_COUNT        3U
#define CLASSIC_BLOCK_BYTES    16U
#define CLASSIC_MINI_BYTES     320U
#define CLASSIC_1K_BYTES       1024U
#define CLASSIC_4K_BYTES       4096U
#define CLASSIC_SHORT_UID      4U
#define CLASSIC_LONG_UID       7U
#define HEX_BYTE_CHARS         2U
#define HEX_RADIX              16U
#define EXTENSION_BYTES        4U
#define V_UID_BYTES            8U
#define GENERATED_PREFIX_BYTES 3U
#define NTAG_PREFIX_BYTES      5U

typedef struct {
  char path[NFC_ARCHIVE_PATH_SIZE];
} nfc_library_path_t;

typedef struct {
  char old_path[NFC_ARCHIVE_PATH_SIZE];
  char new_path[NFC_ARCHIVE_PATH_SIZE];
} nfc_library_move_t;

static const char *TAG = "NFC_LIBRARY";
static atomic_flag s_is_mutating = ATOMIC_FLAG_INIT;
static const char *const COMPANION_EXTENSIONS[COMPANION_COUNT] = {".nfc", ".bin", ".keys"};

static bool valid_filename(const char *name);
static esp_err_t make_path(const char *filename, char *out_path);
static hb_nfc_protocol_t family(hb_nfc_protocol_t protocol);
static bool same_card(const nfc_store_info_t *left, const nfc_store_info_t *right);
static hb_nfc_protocol_t device_protocol(const char *device);
static esp_err_t binary_info(const char *filename, const char *path, nfc_store_info_t *out);
static esp_err_t append_path(nfc_library_path_t *paths, size_t *count, const char *path);
static esp_err_t
collect_paths(const nfc_store_info_t *card, nfc_library_path_t *paths, size_t *out_count);
static int deletion_order(const void *left, const void *right);
static esp_err_t delete_card(const nfc_store_info_t *card);

esp_err_t nfc_library_file_info(const char *filename, nfc_store_info_t *out_info) {
  if (out_info == NULL)
    return ESP_ERR_INVALID_ARG;
  memset(out_info, 0, sizeof(*out_info));
  char path[NFC_ARCHIVE_PATH_SIZE];
  esp_err_t error = make_path(filename, path);
  if (error != ESP_OK)
    return error;
  const char *extension = strrchr(filename, '.');
  if (extension == NULL)
    return ESP_ERR_NOT_SUPPORTED;
  if (strcasecmp(extension, ".bin") == 0)
    return binary_info(filename, path, out_info);
  if (strcasecmp(extension, ".nfc") != 0)
    return ESP_ERR_NOT_SUPPORTED;
  nfc_archive_t archive = {0};
  error = nfc_archive_load(path, &archive);
  if (error != ESP_OK)
    return error;
  out_info->protocol = device_protocol(archive.device);
  if (out_info->protocol == HB_PROTO_UNKNOWN) {
    error = ESP_ERR_NOT_SUPPORTED;
    goto cleanup;
  }
  const char *cursor = archive.uid;
  while (*cursor != '\0' && out_info->uid_len < sizeof(out_info->uid)) {
    char *end;
    out_info->uid[out_info->uid_len++] = (uint8_t)strtoul(cursor, &end, HEX_RADIX);
    cursor = end + strspn(end, " \t");
  }
  if (!archive.is_capture && out_info->protocol == HB_PROTO_ISO15693 &&
      out_info->uid_len == V_UID_BYTES) {
    for (size_t i = 0; i < V_UID_BYTES / 2; i++) {
      uint8_t byte = out_info->uid[i];
      out_info->uid[i] = out_info->uid[V_UID_BYTES - 1 - i];
      out_info->uid[V_UID_BYTES - 1 - i] = byte;
    }
  }
cleanup:
  nfc_archive_free(&archive);
  return error;
}

esp_err_t nfc_library_delete_card(const nfc_store_info_t *card) {
  if (atomic_flag_test_and_set(&s_is_mutating))
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = delete_card(card);
  atomic_flag_clear(&s_is_mutating);
  return error;
}

esp_err_t nfc_library_delete_file(const char *filename, bool *out_card_deleted) {
  if (out_card_deleted == NULL)
    return ESP_ERR_INVALID_ARG;
  *out_card_deleted = false;
  if (atomic_flag_test_and_set(&s_is_mutating))
    return ESP_ERR_INVALID_STATE;
  nfc_store_info_t card;
  esp_err_t error =
      vfs_sdcard_is_mounted() ? nfc_library_file_info(filename, &card) : ESP_ERR_INVALID_STATE;
  if (error == ESP_OK) {
    error = delete_card(&card);
    *out_card_deleted = error == ESP_OK;
  } else if (error == ESP_ERR_INVALID_RESPONSE || error == ESP_ERR_NOT_SUPPORTED ||
             error == ESP_ERR_INVALID_SIZE) {
    char path[NFC_ARCHIVE_PATH_SIZE];
    error = make_path(filename, path);
    if (error == ESP_OK && unlink(path) != 0)
      error = ESP_FAIL;
  }
  atomic_flag_clear(&s_is_mutating);
  return error;
}

esp_err_t nfc_library_rename_file(const char *filename, const char *stem) {
  if (!valid_filename(stem) || strlen(stem) >= NFC_STORE_NAME_MAX)
    return ESP_ERR_INVALID_ARG;
  if (atomic_flag_test_and_set(&s_is_mutating))
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = ESP_ERR_INVALID_STATE;
  nfc_library_move_t *moves = NULL;
  size_t count = 0, moved = 0;
  int vault_index = -1;
  if (!vfs_sdcard_is_mounted())
    goto cleanup;
  char source[NFC_ARCHIVE_PATH_SIZE];
  error = make_path(filename, source);
  if (error != ESP_OK)
    goto cleanup;
  const char *extension = strrchr(filename, '.');
  if (extension == NULL || strcasecmp(extension, ".nfc") != 0) {
    error = ESP_ERR_INVALID_ARG;
    goto cleanup;
  }
  struct stat status;
  if (stat(source, &status) != 0 || !S_ISREG(status.st_mode)) {
    error = ESP_ERR_NOT_FOUND;
    goto cleanup;
  }
  nfc_store_info_t card;
  error = nfc_library_file_info(filename, &card);
  if (error != ESP_OK && error != ESP_ERR_NOT_SUPPORTED && error != ESP_ERR_INVALID_RESPONSE &&
      error != ESP_ERR_INVALID_SIZE)
    goto cleanup;
  bool has_identity = error == ESP_OK;
  int vault_count = 0;
  error = nfc_store_count_checked(&vault_count);
  if (error != ESP_OK)
    goto cleanup;
  for (int index = 0; index < vault_count; index++) {
    nfc_store_info_t info;
    if (nfc_store_get_info(index, &info) != HB_NFC_OK) {
      error = ESP_FAIL;
      goto cleanup;
    }
    if (has_identity && same_card(&card, &info)) {
      if (vault_index >= 0) {
        error = ESP_ERR_INVALID_STATE;
        goto cleanup;
      }
      vault_index = index;
    } else if (strcmp(info.name, stem) == 0) {
      error = ESP_ERR_INVALID_STATE;
      goto cleanup;
    }
  }
  moves = heap_caps_calloc(COMPANION_COUNT, sizeof(*moves), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (moves == NULL) {
    ESP_LOGE(TAG, "Cannot allocate rename plan");
    error = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  for (size_t i = 0; i < COMPANION_COUNT; i++) {
    if (i != 0 && (!has_identity || card.protocol != HB_PROTO_MF_CLASSIC))
      break;
    if (strlen(source) - EXTENSION_BYTES + strlen(COMPANION_EXTENSIONS[i]) >=
        sizeof(moves[count].old_path)) {
      error = ESP_ERR_INVALID_SIZE;
      goto cleanup;
    }
    snprintf(moves[count].old_path,
             sizeof(moves[count].old_path),
             "%.*s%s",
             (int)(strlen(source) - EXTENSION_BYTES),
             source,
             COMPANION_EXTENSIONS[i]);
    if (i == 0)
      strlcpy(moves[count].old_path, source, sizeof(moves[count].old_path));
    if (stat(moves[count].old_path, &status) != 0) {
      if (errno != ENOENT) {
        error = ESP_FAIL;
        goto cleanup;
      }
      continue;
    }
    if (!S_ISREG(status.st_mode)) {
      error = ESP_ERR_INVALID_STATE;
      goto cleanup;
    }
    if (strcmp(COMPANION_EXTENSIONS[i], ".bin") == 0) {
      nfc_store_info_t companion;
      const char *name = strrchr(moves[count].old_path, '/') + 1;
      error = nfc_library_file_info(name, &companion);
      if (error != ESP_OK)
        goto cleanup;
      if (!same_card(&card, &companion)) {
        error = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
      }
    }
    snprintf(moves[count].new_path,
             sizeof(moves[count].new_path),
             NFC_ARCHIVE_DIRECTORY "/%s%s",
             stem,
             COMPANION_EXTENSIONS[i]);
    if (strcmp(moves[count].old_path, moves[count].new_path) == 0)
      continue;
    if (stat(moves[count].new_path, &status) == 0) {
      error = ESP_ERR_INVALID_STATE;
      goto cleanup;
    }
    if (errno != ENOENT) {
      error = ESP_FAIL;
      goto cleanup;
    }
    count++;
  }
  for (; moved < count; moved++) {
    if (rename(moves[moved].old_path, moves[moved].new_path) != 0) {
      error = ESP_FAIL;
      goto rollback;
    }
  }
  error = vault_index >= 0 ? nfc_store_rename(vault_index, stem) : ESP_OK;
  if (error == ESP_OK)
    goto cleanup;
rollback:
  while (moved > 0) {
    moved--;
    if (rename(moves[moved].new_path, moves[moved].old_path) != 0)
      ESP_LOGE(TAG, "Rename rollback failed for %s", moves[moved].old_path);
  }
cleanup:
  free(moves);
  atomic_flag_clear(&s_is_mutating);
  return error;
}

static bool valid_filename(const char *name) {
  if (name == NULL || name[0] == '\0' || name[0] == '.' || name[0] == ' ' ||
      strpbrk(name, "/\\:*?\"<>|\r\n") != NULL)
    return false;
  size_t length = strlen(name);
  return name[length - 1] != ' ' && name[length - 1] != '.';
}

static esp_err_t make_path(const char *filename, char *out_path) {
  if (!valid_filename(filename))
    return ESP_ERR_INVALID_ARG;
  int length = snprintf(out_path, NFC_ARCHIVE_PATH_SIZE, NFC_ARCHIVE_DIRECTORY "/%s", filename);
  return length > 0 && (size_t)length < NFC_ARCHIVE_PATH_SIZE ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static hb_nfc_protocol_t family(hb_nfc_protocol_t protocol) {
  return protocol == HB_PROTO_SLIX ? HB_PROTO_ISO15693 : protocol;
}

static bool same_card(const nfc_store_info_t *left, const nfc_store_info_t *right) {
  return left->uid_len > 0 && left->uid_len <= sizeof(left->uid) &&
         left->uid_len == right->uid_len && family(left->protocol) == family(right->protocol) &&
         memcmp(left->uid, right->uid, left->uid_len) == 0;
}

static hb_nfc_protocol_t device_protocol(const char *device) {
  if (strcasecmp(device, "Mifare Classic") == 0)
    return HB_PROTO_MF_CLASSIC;
  if (strcasecmp(device, "Mifare Plus") == 0)
    return HB_PROTO_MF_PLUS;
  if (strcasecmp(device, "Mifare DESFire") == 0)
    return HB_PROTO_MF_DESFIRE;
  if (strcasecmp(device, "NTAG4xx") == 0 || strcasecmp(device, "Type 4 Tag") == 0 ||
      strcasecmp(device, "ISO14443-4A") == 0)
    return HB_PROTO_ISO14443_4A;
  if (strcasecmp(device, "NTAG/Ultralight") == 0 || strcasecmp(device, "Mifare Ultralight") == 0 ||
      strncasecmp(device, "NTAG2", NTAG_PREFIX_BYTES) == 0)
    return HB_PROTO_MF_ULTRALIGHT;
  if (strcasecmp(device, "ISO15693-3") == 0 || strcasecmp(device, "ISO15693") == 0 ||
      strcasecmp(device, "SLIX") == 0 || strcasecmp(device, "ICODE SLIX") == 0)
    return HB_PROTO_ISO15693;
  if (strcasecmp(device, "FeliCa") == 0)
    return HB_PROTO_FELICA;
  if (strcasecmp(device, "Texkom") == 0)
    return HB_PROTO_TEXKOM;
  if (strcasecmp(device, "ST25TB") == 0)
    return HB_PROTO_ST25TB;
  if (strcasecmp(device, "ISO14443-3A") == 0)
    return HB_PROTO_ISO14443_3A;
  if (strcasecmp(device, "ISO14443-3B") == 0)
    return HB_PROTO_ISO14443_3B;
  if (strcasecmp(device, "ISO14443-4B") == 0)
    return HB_PROTO_ISO14443_4B;
  return HB_PROTO_UNKNOWN;
}

static esp_err_t binary_info(const char *filename, const char *path, nfc_store_info_t *out) {
  struct stat status;
  if (stat(path, &status) != 0)
    return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
  if (status.st_size != CLASSIC_MINI_BYTES && status.st_size != CLASSIC_1K_BYTES &&
      status.st_size != CLASSIC_4K_BYTES)
    return ESP_ERR_INVALID_SIZE;
  uint8_t block[CLASSIC_BLOCK_BYTES];
  FILE *file = fopen(path, "rb");
  if (file == NULL)
    return ESP_FAIL;
  bool has_block = fread(block, 1, sizeof(block), file) == sizeof(block);
  if (fclose(file) != 0 || !has_block)
    return ESP_FAIL;
  char sibling[NFC_ARCHIVE_PATH_SIZE];
  strlcpy(sibling, filename, sizeof(sibling));
  strcpy(sibling + strlen(sibling) - EXTENSION_BYTES, ".nfc");
  esp_err_t error = nfc_library_file_info(sibling, out);
  if (error == ESP_OK) {
    if (out->protocol != HB_PROTO_MF_CLASSIC || memcmp(block, out->uid, out->uid_len) != 0)
      return ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
  }
  if (error != ESP_ERR_NOT_FOUND)
    return error;
  out->protocol = HB_PROTO_MF_CLASSIC;
  out->uid_len = CLASSIC_SHORT_UID;
  // Legacy raw captures encode a longer UID in the generated filename.
  if (strncmp(filename, "mf_", GENERATED_PREFIX_BYTES) == 0) {
    const char *end = strchr(filename + GENERATED_PREFIX_BYTES, '_');
    if (end != NULL &&
        (size_t)(end - filename - GENERATED_PREFIX_BYTES) == CLASSIC_LONG_UID * HEX_BYTE_CHARS) {
      uint8_t uid[CLASSIC_LONG_UID];
      bool is_valid = true;
      for (size_t i = 0; i < sizeof(uid); i++) {
        const char *hex = filename + GENERATED_PREFIX_BYTES + i * HEX_BYTE_CHARS;
        if (!isxdigit((unsigned char)hex[0]) || !isxdigit((unsigned char)hex[1])) {
          is_valid = false;
          break;
        }
        char byte[HEX_BYTE_CHARS + 1] = {hex[0], hex[1], 0};
        uid[i] = (uint8_t)strtoul(byte, NULL, HEX_RADIX);
      }
      if (!is_valid || memcmp(block, uid, sizeof(uid)) != 0)
        return ESP_ERR_INVALID_RESPONSE;
      out->uid_len = sizeof(uid);
    }
  }
  memcpy(out->uid, block, out->uid_len);
  return ESP_OK;
}

static esp_err_t append_path(nfc_library_path_t *paths, size_t *count, const char *path) {
  for (size_t i = 0; i < *count; i++) {
    if (strcmp(paths[i].path, path) == 0)
      return ESP_OK;
  }
  if (*count == LIBRARY_MAX_FILES)
    return ESP_ERR_NO_MEM;
  strlcpy(paths[(*count)++].path, path, NFC_ARCHIVE_PATH_SIZE);
  return ESP_OK;
}

static esp_err_t
collect_paths(const nfc_store_info_t *card, nfc_library_path_t *paths, size_t *out_count) {
  *out_count = 0;
  if (!vfs_sdcard_is_mounted())
    return ESP_ERR_INVALID_STATE;
  DIR *directory = opendir(NFC_ARCHIVE_DIRECTORY);
  if (directory == NULL)
    return errno == ENOENT ? ESP_OK : ESP_FAIL;
  esp_err_t error = ESP_OK;
  struct dirent *entry;
  while (true) {
    errno = 0;
    entry = readdir(directory);
    if (entry == NULL) {
      if (errno != 0)
        error = ESP_FAIL;
      break;
    }
    const char *extension = strrchr(entry->d_name, '.');
    if (extension == NULL ||
        (strcasecmp(extension, ".nfc") != 0 && strcasecmp(extension, ".bin") != 0))
      continue;
    nfc_store_info_t info;
    error = nfc_library_file_info(entry->d_name, &info);
    if (error == ESP_ERR_NO_MEM || error == ESP_FAIL)
      break;
    if (error != ESP_OK || !same_card(card, &info)) {
      error = ESP_OK;
      continue;
    }
    char path[NFC_ARCHIVE_PATH_SIZE];
    error = make_path(entry->d_name, path);
    if (error != ESP_OK || (error = append_path(paths, out_count, path)) != ESP_OK)
      break;
    if (card->protocol == HB_PROTO_MF_CLASSIC) {
      if (strlen(path) + 1 >= sizeof(path)) {
        error = ESP_ERR_INVALID_SIZE;
        break;
      }
      strcpy(strrchr(path, '.'), ".keys");
      struct stat status;
      int found = stat(path, &status);
      if (found != 0 && errno != ENOENT) {
        error = ESP_FAIL;
        break;
      }
      if (found == 0 && S_ISREG(status.st_mode)) {
        error = append_path(paths, out_count, path);
        if (error != ESP_OK)
          break;
      }
    }
  }
  if (closedir(directory) != 0 && error == ESP_OK)
    error = ESP_FAIL;
  return error;
}

static int deletion_order(const void *left, const void *right) {
  const nfc_library_path_t *a = left;
  const nfc_library_path_t *b = right;
  bool a_is_profile = strcasecmp(strrchr(a->path, '.'), ".nfc") == 0;
  bool b_is_profile = strcasecmp(strrchr(b->path, '.'), ".nfc") == 0;
  return (int)a_is_profile - (int)b_is_profile;
}

static esp_err_t delete_card(const nfc_store_info_t *card) {
  if (card == NULL || card->uid_len == 0 || card->uid_len > sizeof(card->uid))
    return ESP_ERR_INVALID_ARG;
  nfc_library_path_t *paths =
      heap_caps_malloc(LIBRARY_MAX_FILES * sizeof(*paths), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (paths == NULL) {
    ESP_LOGE(TAG, "Cannot allocate deletion plan");
    return ESP_ERR_NO_MEM;
  }
  size_t count = 0;
  int vault_count = 0;
  esp_err_t error = nfc_store_count_checked(&vault_count);
  if (error != ESP_OK)
    goto cleanup;
  error = collect_paths(card, paths, &count);
  if (error != ESP_OK)
    goto cleanup;
  // Keep full UID metadata until its binary/key companions have been removed.
  qsort(paths, count, sizeof(*paths), deletion_order);
  for (size_t i = 0; i < count; i++) {
    if (unlink(paths[i].path) != 0 && errno != ENOENT) {
      ESP_LOGE(TAG, "Cannot delete %s", paths[i].path);
      error = ESP_FAIL;
      goto cleanup;
    }
  }
  for (int index = vault_count - 1; index >= 0; index--) {
    nfc_store_info_t info;
    if (nfc_store_get_info(index, &info) != HB_NFC_OK) {
      error = ESP_FAIL;
      goto cleanup;
    }
    if (same_card(card, &info) && nfc_store_delete(index) != HB_NFC_OK) {
      error = ESP_FAIL;
      goto cleanup;
    }
  }
cleanup:
  free(paths);
  return error;
}
