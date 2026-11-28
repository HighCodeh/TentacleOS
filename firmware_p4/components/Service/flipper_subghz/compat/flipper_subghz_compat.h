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

#ifndef FLIPPER_SUBGHZ_COMPAT_H
#define FLIPPER_SUBGHZ_COMPAT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <limits.h>
#include <setjmp.h>
#include <ctype.h>
#define UNUSED(x)          ((void)(x))
#define COUNT_OF(x)        (sizeof(x) / sizeof((x)[0]))
#define MIN(a, b)          ((a) < (b) ? (a) : (b))
#define MAX(a, b)          ((a) > (b) ? (a) : (b))
#define CLAMP(x, max, min) MIN(MAX(x, min), max)
#define EXT_PATH(x)        "/sdcard/subghz/" x
#define FURI_LOG_E(...)    ((void)0)
#define FURI_LOG_W(...)    ((void)0)
#define FURI_LOG_I(...)    ((void)0)
#define FURI_LOG_D(...)    ((void)0)
#define FURI_LOG_T(...)    ((void)0)
#define furi_assert(x)        \
  do {                        \
    if (!(x))                 \
      flipper_subghz_fault(); \
  } while (0)
#define furi_check(x, ...)                         furi_assert(x)
#define furi_crash(...)                            flipper_subghz_fault()
#define subghz_protocol_registry_get_by_name       flipper_subghz_upstream_get_by_name
#define subghz_protocol_registry_get_by_index      flipper_subghz_upstream_get_by_index
#define subghz_protocol_registry_count             flipper_subghz_upstream_count
#define memmgr_get_free_heap()                     flipper_subghz_available()
#define memmgr_heap_get_max_free_block()           flipper_subghz_available()
#define FuriHalRtcLocaleUnitsMetric                0
#define furi_hal_rtc_get_locale_units()            0
#define furi_hal_subghz_get_rolling_counter_mult() 1
#define FURI_PACKED                                __attribute__((packed))
#define RECORD_STORAGE                             "storage"
#define float_is_equal(a, b)                       (fabsf((a) - (b)) < 0.00001f)
void flipper_subghz_fault(void);
void *flipper_subghz_malloc(size_t size);
void flipper_subghz_free(void *ptr);
size_t flipper_subghz_mark(void);
size_t flipper_subghz_available(void);
void flipper_subghz_rewind(size_t mark);
#ifndef FLIPPER_SUBGHZ_PLATFORM_IMPL
#define malloc flipper_subghz_malloc
#define free   flipper_subghz_free
#endif
#define FLIPPER_SUBGHZ_TEXT_CAPACITY   8192U
#define FLIPPER_SUBGHZ_FORMAT_CAPACITY 16384U

typedef struct {
  char *text;
  size_t capacity;
} FuriString;
typedef struct {
  char text[FLIPPER_SUBGHZ_FORMAT_CAPACITY];
  size_t cursor;
  bool failed;
} FlipperFormat;
typedef FlipperFormat Stream;
FuriString *furi_string_alloc(void);
FuriString *furi_string_alloc_set(const char *value);
void furi_string_free(FuriString *s);
const char *furi_string_get_cstr(const FuriString *s);
void furi_string_set(FuriString *s, const char *value);
void furi_string_set_n(FuriString *s, const FuriString *value, size_t start, size_t n);
void furi_string_set_strn(FuriString *s, const char *value, size_t n);
void furi_string_reset(FuriString *s);
bool furi_string_empty(const FuriString *s);
size_t furi_string_size(const FuriString *s);
char furi_string_get_char(const FuriString *s, size_t index);
void furi_string_push_back(FuriString *s, char c);
void furi_string_cat(FuriString *s, const char *value);
void furi_string_cat_str(FuriString *s, const char *value);
void furi_string_printf(FuriString *s, const char *format, ...);
void furi_string_cat_printf(FuriString *s, const char *format, ...);
void furi_string_trim(FuriString *s);
bool flipper_format_rewind(FlipperFormat *f);
Stream *flipper_format_get_raw_stream(FlipperFormat *f);
void stream_clean(Stream *s);
bool flipper_format_read_string(FlipperFormat *f, const char *key, FuriString *s);
bool flipper_format_write_string_cstr(FlipperFormat *f, const char *key, const char *s);
bool flipper_format_update_string_cstr(FlipperFormat *f, const char *key, const char *s);
bool flipper_format_write_header_cstr(FlipperFormat *f, const char *type, uint32_t version);
#define flipper_format_write_string(f, k, s) \
  flipper_format_write_string_cstr(f, k, furi_string_get_cstr(s))
#define DECLARE_FORMAT(type, name)                                                              \
  bool flipper_format_read_##name(FlipperFormat *f, const char *key, type *data, size_t count); \
  bool flipper_format_write_##name(                                                             \
      FlipperFormat *f, const char *key, const type *data, size_t count);                       \
  bool flipper_format_update_##name(                                                            \
      FlipperFormat *f, const char *key, const type *data, size_t count);                       \
  bool flipper_format_insert_or_update_##name(                                                  \
      FlipperFormat *f, const char *key, const type *data, size_t count);
DECLARE_FORMAT(uint32_t, uint32)
DECLARE_FORMAT(int32_t, int32)
DECLARE_FORMAT(uint8_t, hex)
DECLARE_FORMAT(bool, bool)
DECLARE_FORMAT(float, float)
bool furi_hal_crypto_aes128_ecb_encrypt(const uint8_t *key, const uint8_t *in, uint8_t *out);
bool furi_hal_crypto_aes128_ecb_decrypt(const uint8_t *key, const uint8_t *in, uint8_t *out);
#endif
