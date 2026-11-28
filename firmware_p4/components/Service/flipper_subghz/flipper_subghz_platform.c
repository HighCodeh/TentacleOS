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

#define FLIPPER_SUBGHZ_PLATFORM_IMPL
#include "flipper_subghz_compat.h"
#include "flipper_subghz.h"
#include "lib/subghz/subghz_keystore.h"
#include "lib/subghz/subghz_keystore_i.h"
#include <errno.h>
#include <stdatomic.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "psa/crypto.h"
#else
#include <openssl/aes.h>
#endif

#define ARENA_SIZE          (512U * 1024U)
#define ARENA_ALIGNMENT     16U
#define STRING_INITIAL_SIZE 128U
#define KEY_CAPACITY        512U
#define KEY_LINE_SIZE       256U
#define PORT_AES_BLOCK_SIZE 16U
#define BYTE_BITS           8U
#define FIELD_NUMBER_SIZE   64U

static uint8_t *s_arena;
static size_t s_used;
static int s_failure;
static jmp_buf s_fault;
static atomic_flag s_lock = ATOMIC_FLAG_INIT;
#ifdef ESP_PLATFORM
static const char *TAG = "FLIPPER_CODEC";
#endif

void flipper_subghz_fault(void) {
  if (s_failure == 0)
    s_failure = ESP_ERR_INVALID_RESPONSE;
  longjmp(s_fault, 1);
}

void *flipper_subghz_malloc(size_t size) {
  if (size > ARENA_SIZE || s_used > ARENA_SIZE - size ||
      ARENA_ALIGNMENT > ARENA_SIZE - s_used - size) {
    s_failure = ESP_ERR_NO_MEM;
    flipper_subghz_fault();
  }
  void *result = s_arena + s_used;
  s_used += (size + ARENA_ALIGNMENT - 1U) & ~(ARENA_ALIGNMENT - 1U);
  memset(result, 0, size);
  return result;
}

void flipper_subghz_free(void *ptr) {
  UNUSED(ptr);
}
size_t flipper_subghz_mark(void) {
  return s_used;
}
size_t flipper_subghz_available(void) {
  return ARENA_SIZE - s_used;
}
void flipper_subghz_rewind(size_t mark) {
  furi_check(mark <= s_used);
  s_used = mark;
}

esp_err_t flipper_subghz_guard(flipper_subghz_job_cb_t job, void *context) {
  if (atomic_flag_test_and_set(&s_lock))
    return ESP_ERR_INVALID_STATE;
#ifdef ESP_PLATFORM
  s_arena = heap_caps_malloc(ARENA_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  s_arena = malloc(ARENA_SIZE);
#endif
  if (s_arena == NULL) {
#ifdef ESP_PLATFORM
    ESP_LOGE(TAG, "Cannot allocate codec workspace in PSRAM");
#endif
    atomic_flag_clear(&s_lock);
    return ESP_ERR_NO_MEM;
  }
  s_used = 0;
  s_failure = 0;
  esp_err_t result = setjmp(s_fault) == 0 ? job(context) : s_failure;
  free(s_arena);
  s_arena = NULL;
  atomic_flag_clear(&s_lock);
  return result;
}

static void string_reserve(FuriString *s, size_t size) {
  if (size > FLIPPER_SUBGHZ_TEXT_CAPACITY)
    flipper_subghz_fault();
  if (size <= s->capacity)
    return;
  char *text = flipper_subghz_malloc(size);
  if (s->text != NULL)
    memcpy(text, s->text, strlen(s->text) + 1);
  s->text = text;
  s->capacity = size;
}
FuriString *furi_string_alloc(void) {
  FuriString *s = flipper_subghz_malloc(sizeof(*s));
  string_reserve(s, STRING_INITIAL_SIZE);
  return s;
}
FuriString *furi_string_alloc_set(const char *value) {
  FuriString *s = furi_string_alloc();
  furi_string_set(s, value);
  return s;
}
void furi_string_free(FuriString *s) {
  UNUSED(s);
}
const char *furi_string_get_cstr(const FuriString *s) {
  return s->text;
}
void furi_string_set_strn(FuriString *s, const char *value, size_t n) {
  string_reserve(s, n + 1);
  memmove(s->text, value, n);
  s->text[n] = 0;
}
void furi_string_set(FuriString *s, const char *value) {
  furi_string_set_strn(s, value, strlen(value));
}
void furi_string_set_n(FuriString *s, const FuriString *value, size_t start, size_t n) {
  size_t length = strlen(value->text);
  furi_check(start <= length && n <= length - start);
  furi_string_set_strn(s, value->text + start, n);
}
void furi_string_reset(FuriString *s) {
  s->text[0] = 0;
}
bool furi_string_empty(const FuriString *s) {
  return s->text[0] == 0;
}
size_t furi_string_size(const FuriString *s) {
  return strlen(s->text);
}
char furi_string_get_char(const FuriString *s, size_t index) {
  furi_check(index < strlen(s->text));
  return s->text[index];
}
void furi_string_cat(FuriString *s, const char *value) {
  size_t length = strlen(s->text), extra = strlen(value);
  string_reserve(s, length + extra + 1);
  memcpy(s->text + length, value, extra + 1);
}
void furi_string_cat_str(FuriString *s, const char *value) {
  furi_string_cat(s, value);
}
void furi_string_push_back(FuriString *s, char c) {
  char value[] = {c, 0};
  furi_string_cat(s, value);
}
static void string_vprintf(FuriString *s, size_t offset, const char *format, va_list args) {
  va_list copy;
  va_copy(copy, args);
  int count = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (count < 0)
    flipper_subghz_fault();
  string_reserve(s, offset + (size_t)count + 1);
  vsnprintf(s->text + offset, s->capacity - offset, format, args);
}
void furi_string_printf(FuriString *s, const char *format, ...) {
  va_list args;
  va_start(args, format);
  string_vprintf(s, 0, format, args);
  va_end(args);
}
void furi_string_cat_printf(FuriString *s, const char *format, ...) {
  va_list args;
  va_start(args, format);
  string_vprintf(s, strlen(s->text), format, args);
  va_end(args);
}
void furi_string_trim(FuriString *s) {
  char *begin = s->text;
  while (isspace((unsigned char)*begin))
    ++begin;
  size_t count = strlen(begin);
  while (count && isspace((unsigned char)begin[count - 1]))
    --count;
  memmove(s->text, begin, count);
  s->text[count] = 0;
}

bool flipper_format_rewind(FlipperFormat *f) {
  f->cursor = 0;
  return !f->failed;
}
Stream *flipper_format_get_raw_stream(FlipperFormat *f) {
  return f;
}
void stream_clean(Stream *f) {
  f->text[0] = 0;
  f->cursor = 0;
  f->failed = false;
}

static const char *find_value(FlipperFormat *f, const char *key) {
  size_t key_len = strlen(key), length = strlen(f->text);
  for (size_t offset = f->cursor; offset < length;) {
    const char *line = f->text + offset;
    const char *end = strchr(line, '\n');
    size_t next = end ? (size_t)(end - f->text) + 1 : length;
    if (strncmp(line, key, key_len) == 0 && line[key_len] == ':') {
      f->cursor = next;
      const char *value = line + key_len + 1;
      while (*value == ' ' || *value == '\t')
        ++value;
      return value;
    }
    offset = next;
  }
  return NULL;
}
static bool write_value(FlipperFormat *f, const char *key, const char *value, bool update) {
  if (f->failed || strchr(key, '\n') || strchr(value, '\n') || strchr(value, '\r'))
    return false;
  size_t length = strlen(f->text), start = length, end = length, key_len = strlen(key);
  if (update) {
    for (size_t offset = 0; offset < length;) {
      char *line = f->text + offset;
      char *next = strchr(line, '\n');
      size_t following = next ? (size_t)(next - f->text) + 1 : length;
      if (strncmp(line, key, key_len) == 0 && line[key_len] == ':') {
        start = offset;
        end = following;
        break;
      }
      offset = following;
    }
  }
  size_t entry_size = key_len + strlen(value) + 3;
  if (entry_size >= sizeof(f->text) || length - (end - start) >= sizeof(f->text) - entry_size) {
    f->failed = true;
    return false;
  }
  memmove(f->text + start + entry_size, f->text + end, length - end + 1);
  memcpy(f->text + start, key, key_len);
  f->text[start + key_len] = ':';
  f->text[start + key_len + 1] = ' ';
  memcpy(f->text + start + key_len + 2, value, strlen(value));
  f->text[start + entry_size - 1] = '\n';
  f->cursor = 0;
  return true;
}
bool flipper_format_read_string(FlipperFormat *f, const char *key, FuriString *s) {
  const char *v = find_value(f, key);
  if (!v)
    return false;
  furi_string_set_strn(s, v, strcspn(v, "\r\n"));
  return true;
}
bool flipper_format_write_string_cstr(FlipperFormat *f, const char *key, const char *s) {
  return write_value(f, key, s, false);
}
bool flipper_format_update_string_cstr(FlipperFormat *f, const char *key, const char *s) {
  return write_value(f, key, s, true);
}
bool flipper_format_write_header_cstr(FlipperFormat *f, const char *type, uint32_t version) {
  return flipper_format_write_string_cstr(f, "Filetype", type) &&
         flipper_format_write_uint32(f, "Version", &version, 1);
}
static bool
read_numbers(FlipperFormat *f, const char *key, void *data, size_t count, unsigned kind) {
  const char *v = find_value(f, key);
  if (!v || !data || count == 0)
    return false;
  for (size_t i = 0; i < count; i++) {
    while (*v == ' ' || *v == '\t')
      ++v;
    if (*v == '\n' || *v == '\r' || *v == 0 || *v == '-')
      return false;
    char *end;
    errno = 0;
    unsigned long long x = strtoull(v, &end, kind == 1 ? 16 : 10);
    if (errno || end == v || x > (kind == 1 ? UINT8_MAX : UINT32_MAX))
      return false;
    if (kind == 1)
      ((uint8_t *)data)[i] = (uint8_t)x;
    else
      ((uint32_t *)data)[i] = (uint32_t)x;
    v = end;
  }
  while (*v == ' ' || *v == '\t')
    ++v;
  if (kind == 1 && count == 8 && strcmp(key, "Key") == 0 &&
      (strstr(f->text, "Protocol: Hormann BiSecur\n") != NULL ||
       strstr(f->text, "Protocol: Hormann BiSecur\r\n") != NULL))
    return true;
  return *v == 0 || *v == '\n' || *v == '\r';
}
bool flipper_format_read_uint32(FlipperFormat *f, const char *k, uint32_t *d, size_t n) {
  return read_numbers(f, k, d, n, 0);
}
bool flipper_format_read_hex(FlipperFormat *f, const char *k, uint8_t *d, size_t n) {
  return read_numbers(f, k, d, n, 1);
}
bool flipper_format_read_bool(FlipperFormat *f, const char *k, bool *d, size_t n) {
  const char *v = find_value(f, k);
  if (!v || n != 1)
    return false;
  size_t length = strcspn(v, "\r\n");
  while (length && (v[length - 1] == ' ' || v[length - 1] == '\t'))
    --length;
  if (length == 4 && memcmp(v, "true", 4) == 0) {
    *d = true;
    return true;
  }
  if (length == 5 && memcmp(v, "false", 5) == 0) {
    *d = false;
    return true;
  }
  return false;
}
static bool write_numbers(
    FlipperFormat *f, const char *key, const void *data, size_t count, unsigned kind, bool update) {
  size_t unit = kind == 1 ? 3U : FIELD_NUMBER_SIZE;
  if (!data || count == 0 || count > (FLIPPER_SUBGHZ_FORMAT_CAPACITY - 1) / unit)
    return false;
  size_t capacity = count * unit + 1;
  size_t mark = flipper_subghz_mark();
  char *value = flipper_subghz_malloc(capacity);
  size_t offset = 0;
  for (size_t i = 0; i < count; i++) {
    int n;
    if (kind == 1)
      n = snprintf(
          value + offset, capacity - offset, "%s%02X", i ? " " : "", ((const uint8_t *)data)[i]);
    else if (kind == 2)
      n = snprintf(value + offset,
                   capacity - offset,
                   "%s%.6f",
                   i ? " " : "",
                   (double)((const float *)data)[i]);
    else
      n = snprintf(value + offset,
                   capacity - offset,
                   "%s%lu",
                   i ? " " : "",
                   (unsigned long)((const uint32_t *)data)[i]);
    if (n < 0 || n >= FIELD_NUMBER_SIZE) {
      flipper_subghz_rewind(mark);
      return false;
    }
    offset += (size_t)n;
  }
  bool result = write_value(f, key, value, update);
  flipper_subghz_rewind(mark);
  return result;
}
#define WRITE_FUNCTIONS(type, name, kind)                                                       \
  bool flipper_format_write_##name(FlipperFormat *f, const char *k, const type *d, size_t n) {  \
    return write_numbers(f, k, d, n, kind, false);                                              \
  }                                                                                             \
  bool flipper_format_update_##name(FlipperFormat *f, const char *k, const type *d, size_t n) { \
    return write_numbers(f, k, d, n, kind, true);                                               \
  }                                                                                             \
  bool flipper_format_insert_or_update_##name(                                                  \
      FlipperFormat *f, const char *k, const type *d, size_t n) {                               \
    return write_numbers(f, k, d, n, kind, true);                                               \
  }
WRITE_FUNCTIONS(uint32_t, uint32, 0)
WRITE_FUNCTIONS(uint8_t, hex, 1)
WRITE_FUNCTIONS(float, float, 2)

SubGhzKeystore *subghz_keystore_alloc(void) {
  SubGhzKeystore *store = flipper_subghz_malloc(sizeof(*store));
  store->data[0].items = flipper_subghz_malloc(KEY_CAPACITY * sizeof(SubGhzKey));
  store->mfname = "";
  return store;
}
void subghz_keystore_free(SubGhzKeystore *store) {
  UNUSED(store);
}
SubGhzKeyArray_t *subghz_keystore_get_data(SubGhzKeystore *store) {
  return &store->data;
}
void subghz_keystore_reset_kl(SubGhzKeystore *store) {
  store->mfname = "";
  store->kl_type = 0;
}
bool subghz_keystore_load(SubGhzKeystore *store, const char *path) {
  FILE *file = fopen(path, "r");
  if (!file)
    return false;
  char line[KEY_LINE_SIZE];
  char maker[KEY_LINE_SIZE] = {0};
  uint64_t pending_key = 0;
  bool has_key = false;
  bool valid = true;
  size_t initial_count = store->data[0].count;
  while (fgets(line, sizeof(line), file)) {
    uint64_t key = 0;
    unsigned long type = 0;
    char *name = NULL;
    char *end = NULL;
    if (strncmp(line, "Manufacturer:", strlen("Manufacturer:")) == 0) {
      const char *value = line + strlen("Manufacturer:");
      while (*value == ' ')
        ++value;
      snprintf(maker, sizeof(maker), "%s", value);
      maker[strcspn(maker, "\r\n")] = 0;
      has_key = false;
      continue;
    }
    if (strncmp(line, "Key (Hex):", strlen("Key (Hex):")) == 0) {
      errno = 0;
      const char *value = line + strlen("Key (Hex):");
      pending_key = strtoull(value, &end, 16);
      has_key = errno == 0 && end != value;
      continue;
    }
    if (strncmp(line, "Type:", strlen("Type:")) == 0 && has_key && maker[0]) {
      errno = 0;
      type = strtoul(line + strlen("Type:"), &end, 10);
      if (errno || type > UINT16_MAX) {
        valid = false;
        break;
      }
      key = pending_key;
      name = maker;
      has_key = false;
    } else if (isxdigit((unsigned char)line[0])) {
      errno = 0;
      key = strtoull(line, &end, 16);
      if (errno || end - line != 16 || *end != ':')
        continue;
      type = strtoul(end + 1, &end, 10);
      if (*end != ':' || type > UINT16_MAX) {
        valid = false;
        break;
      }
      name = end + 1;
      name[strcspn(name, "\r\n")] = 0;
    } else {
      continue;
    }
    if (name == NULL || !name[0] || store->data[0].count == KEY_CAPACITY) {
      valid = false;
      break;
    }
    SubGhzKey *entry = &store->data[0].items[store->data[0].count++];
    entry->key = key;
    entry->type = (uint16_t)type;
    entry->name = furi_string_alloc_set(name);
  }
  if (ferror(file))
    valid = false;
  if (fclose(file) != 0)
    valid = false;
  if (!valid)
    store->data[0].count = initial_count;
  return valid && store->data[0].count > initial_count;
}
bool subghz_keystore_raw_get_data(const char *path, size_t offset, uint8_t *data, size_t count) {
  if (!path || offset > LONG_MAX)
    return false;
  FILE *file = fopen(path, "rb");
  if (!file)
    return false;
  char header[9] = {0};
  bool ok = fread(header, 1, 8, file) == 8 && memcmp(header, "Filetype", 8) != 0;
  ok = ok && fseek(file, (long)offset, SEEK_SET) == 0 && fread(data, 1, count, file) == count;
  if (fclose(file) != 0)
    ok = false;
  return ok;
}
static bool aes_block(const uint8_t *key, const uint8_t *in, uint8_t *out, bool encrypt) {
#ifdef ESP_PLATFORM
  psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
  mbedtls_svc_key_id_t id = MBEDTLS_SVC_KEY_ID_INIT;
  psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
  psa_set_key_bits(&attributes, PORT_AES_BLOCK_SIZE * BYTE_BITS);
  psa_set_key_usage_flags(&attributes, encrypt ? PSA_KEY_USAGE_ENCRYPT : PSA_KEY_USAGE_DECRYPT);
  psa_set_key_algorithm(&attributes, PSA_ALG_ECB_NO_PADDING);
  psa_status_t status = psa_crypto_init();
  if (status == PSA_SUCCESS)
    status = psa_import_key(&attributes, key, PORT_AES_BLOCK_SIZE, &id);
  size_t written = 0;
  if (status == PSA_SUCCESS) {
    status = encrypt ? psa_cipher_encrypt(id,
                                          PSA_ALG_ECB_NO_PADDING,
                                          in,
                                          PORT_AES_BLOCK_SIZE,
                                          out,
                                          PORT_AES_BLOCK_SIZE,
                                          &written)
                     : psa_cipher_decrypt(id,
                                          PSA_ALG_ECB_NO_PADDING,
                                          in,
                                          PORT_AES_BLOCK_SIZE,
                                          out,
                                          PORT_AES_BLOCK_SIZE,
                                          &written);
    psa_destroy_key(id);
  }
  psa_reset_key_attributes(&attributes);
  return status == PSA_SUCCESS && written == PORT_AES_BLOCK_SIZE;
#else
  AES_KEY context;
  if (encrypt) {
    if (AES_set_encrypt_key(key, PORT_AES_BLOCK_SIZE * BYTE_BITS, &context) != 0)
      return false;
    AES_encrypt(in, out, &context);
  } else {
    if (AES_set_decrypt_key(key, PORT_AES_BLOCK_SIZE * BYTE_BITS, &context) != 0)
      return false;
    AES_decrypt(in, out, &context);
  }
  return true;
#endif
}
bool furi_hal_crypto_aes128_ecb_encrypt(const uint8_t *key, const uint8_t *in, uint8_t *out) {
  return aes_block(key, in, out, true);
}
bool furi_hal_crypto_aes128_ecb_decrypt(const uint8_t *key, const uint8_t *in, uint8_t *out) {
  return aes_block(key, in, out, false);
}
