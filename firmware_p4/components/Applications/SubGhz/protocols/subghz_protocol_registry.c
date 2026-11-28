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

#include "subghz_protocol_registry.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "flipper_subghz.h"

_Static_assert(SUBGHZ_PROTOCOL_FIELDS_SIZE == FLIPPER_SUBGHZ_FIELDS_SIZE,
               "Protocol field capacity mismatch");
_Static_assert(SUBGHZ_KEY_TEXT_SIZE == FLIPPER_SUBGHZ_KEY_SIZE, "Protocol key capacity mismatch");

#define FIELD_TEXT_SIZE  384U
#define DEFAULT_TE_US    350U
#define DEFAULT_GUARD    30U
#define SELFTEST_PULSES  256U
#define SELFTEST_REPEATS 4U
static const char *TAG = "PROTOCOL_REGISTRY";

static bool
decode_named(const char *name, const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  if (out_data == NULL)
    return false;
  flipper_subghz_result_t *result =
      heap_caps_calloc(1, sizeof(*result), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (result == NULL) {
    ESP_LOGE(TAG, "Cannot allocate decoded fields");
    return false;
  }
  esp_err_t error = flipper_subghz_decode_named(name, pulses, count, result);
  if (error == ESP_OK) {
    memset(out_data, 0, sizeof(*out_data));
    out_data->protocol_name = result->name;
    out_data->bit_count = result->bits;
    out_data->raw_value = (uint32_t)result->key;
    out_data->serial = (uint32_t)result->key;
    memcpy(out_data->key_text, result->key_text, sizeof(out_data->key_text));
    memcpy(out_data->protocol_fields, result->fields, sizeof(out_data->protocol_fields));
    const char *te = strstr(result->fields, "\nTE: ");
    const char *guard = strstr(result->fields, "\nGuard_time: ");
    if (te != NULL)
      out_data->te_us = (uint32_t)strtoul(te + strlen("\nTE: "), NULL, 10);
    if (guard != NULL)
      out_data->guard_time = (uint32_t)strtoul(guard + strlen("\nGuard_time: "), NULL, 10);
  }
  free(result);
  return error == ESP_OK;
}

static size_t encode(const subghz_data_t *data, int32_t *pulses, size_t capacity) {
  if (data == NULL || data->protocol_name == NULL)
    return 0;
  char fields[FIELD_TEXT_SIZE];
  const char *text = data->protocol_fields;
  if (text[0] == '\0') {
    if (data->bit_count > 32)
      return 0;
    int length = snprintf(
        fields,
        sizeof(fields),
        "Protocol: %s\nBit: %u\nKey: 00 00 00 00 %02X %02X %02X %02X\nTE: %lu\nGuard_time: %lu\n",
        data->protocol_name,
        (unsigned)data->bit_count,
        (unsigned)(data->raw_value >> 24),
        (unsigned)((data->raw_value >> 16) & 255),
        (unsigned)((data->raw_value >> 8) & 255),
        (unsigned)(data->raw_value & 255),
        (unsigned long)(data->te_us ? data->te_us : DEFAULT_TE_US),
        (unsigned long)(data->guard_time ? data->guard_time : DEFAULT_GUARD));
    if (length < 0 || (size_t)length >= sizeof(fields))
      return 0;
    text = fields;
  }
  size_t count = 0;
  return flipper_subghz_encode(text, pulses, capacity, &count) == ESP_OK ? count : 0;
}

static bool decode_0(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("GateTX", pulses, count, out_data);
}

static bool decode_1(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("KeeLoq", pulses, count, out_data);
}

static bool decode_2(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Nice FLO", pulses, count, out_data);
}

static bool decode_3(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("CAME", pulses, count, out_data);
}

static bool decode_4(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Prastel", pulses, count, out_data);
}

static bool decode_5(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Faac SLH", pulses, count, out_data);
}

static bool decode_6(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Nice FloR-S", pulses, count, out_data);
}

static bool decode_7(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("CAME TWEE", pulses, count, out_data);
}

static bool decode_8(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("CAME Atomo", pulses, count, out_data);
}

static bool decode_9(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Nero Sketch", pulses, count, out_data);
}

static bool decode_10(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("iDo 117/111", pulses, count, out_data);
}

static bool decode_11(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Hormann HSM", pulses, count, out_data);
}

static bool decode_12(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Nero Radio", pulses, count, out_data);
}

static bool decode_13(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Somfy Telis", pulses, count, out_data);
}

static bool decode_14(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Somfy Keytis", pulses, count, out_data);
}

static bool decode_15(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Princeton", pulses, count, out_data);
}

static bool decode_16(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Linear", pulses, count, out_data);
}

static bool decode_17(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Security+ 2.0", pulses, count, out_data);
}

static bool decode_18(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Security+ 1.0", pulses, count, out_data);
}

static bool decode_19(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("MegaCode", pulses, count, out_data);
}

static bool decode_20(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Holtek", pulses, count, out_data);
}

static bool decode_21(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Cham_Code", pulses, count, out_data);
}

static bool decode_22(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Power Smart", pulses, count, out_data);
}

static bool decode_23(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Marantec", pulses, count, out_data);
}

static bool decode_24(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("BETT", pulses, count, out_data);
}

static bool decode_25(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Doitrand", pulses, count, out_data);
}

static bool decode_26(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Phoenix_V2", pulses, count, out_data);
}

static bool decode_27(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Honeywell Sec", pulses, count, out_data);
}

static bool decode_28(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Honeywell", pulses, count, out_data);
}

static bool decode_29(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Magellan", pulses, count, out_data);
}

static bool decode_30(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Intertechno_V3", pulses, count, out_data);
}

static bool decode_31(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Clemsa", pulses, count, out_data);
}

static bool decode_32(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Ansonic", pulses, count, out_data);
}

static bool decode_33(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("SMC5326", pulses, count, out_data);
}

static bool decode_34(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Holtek_HT12X", pulses, count, out_data);
}

static bool decode_35(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("LinearDelta3", pulses, count, out_data);
}

static bool decode_36(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Dooya", pulses, count, out_data);
}

static bool decode_37(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Alutech AT-4N", pulses, count, out_data);
}

static bool decode_38(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("KingGates Stylo4k", pulses, count, out_data);
}

static bool decode_39(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("BinRAW", pulses, count, out_data);
}

static bool decode_40(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Mastercode", pulses, count, out_data);
}

static bool decode_41(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("X10", pulses, count, out_data);
}

static bool decode_42(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Hormann BiSecur", pulses, count, out_data);
}

static bool decode_43(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Legrand", pulses, count, out_data);
}

static bool decode_44(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Dickert_MAHS", pulses, count, out_data);
}

static bool decode_45(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("GangQi", pulses, count, out_data);
}

static bool decode_46(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Marantec24", pulses, count, out_data);
}

static bool decode_47(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Hollarm", pulses, count, out_data);
}

static bool decode_48(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Hay21", pulses, count, out_data);
}

static bool decode_49(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Revers_RB2", pulses, count, out_data);
}

static bool decode_50(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Feron", pulses, count, out_data);
}

static bool decode_51(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Roger", pulses, count, out_data);
}

static bool decode_52(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Elplast", pulses, count, out_data);
}

static bool decode_53(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Treadmill37", pulses, count, out_data);
}

static bool decode_54(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Beninca ARC", pulses, count, out_data);
}

static bool decode_55(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Jarolift", pulses, count, out_data);
}

static bool decode_56(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Ditec GOL4", pulses, count, out_data);
}

static bool decode_57(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Doorbell32", pulses, count, out_data);
}

static bool decode_58(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("KeyFinder", pulses, count, out_data);
}

static bool decode_59(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("KeyFinder2", pulses, count, out_data);
}

static bool decode_60(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Nord ICE", pulses, count, out_data);
}

static bool decode_61(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Allstar Firefly", pulses, count, out_data);
}

static bool decode_62(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Telcoma/Cardin EDGE", pulses, count, out_data);
}

static bool decode_63(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Cardin S508", pulses, count, out_data);
}

static bool decode_64(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Freezer", pulses, count, out_data);
}

static bool decode_65(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Faac TM", pulses, count, out_data);
}

static bool decode_66(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("Proteco", pulses, count, out_data);
}

static bool decode_67(const int32_t *pulses, size_t count, subghz_data_t *out_data) {
  return decode_named("BelKorn", pulses, count, out_data);
}

static const subghz_protocol_t REGISTERED_PROTOCOLS[] = {
    {"GateTX", decode_0, encode},
    {"KeeLoq", decode_1, encode},
    {"Nice FLO", decode_2, encode},
    {"CAME", decode_3, encode},
    {"Prastel", decode_4, encode},
    {"Faac SLH", decode_5, encode},
    {"Nice FloR-S", decode_6, encode},
    {"CAME TWEE", decode_7, encode},
    {"CAME Atomo", decode_8, encode},
    {"Nero Sketch", decode_9, encode},
    {"iDo 117/111", decode_10, NULL},
    {"Hormann HSM", decode_11, encode},
    {"Nero Radio", decode_12, encode},
    {"Somfy Telis", decode_13, encode},
    {"Somfy Keytis", decode_14, encode},
    {"Princeton", decode_15, encode},
    {"Linear", decode_16, encode},
    {"Security+ 2.0", decode_17, encode},
    {"Security+ 1.0", decode_18, encode},
    {"MegaCode", decode_19, encode},
    {"Holtek", decode_20, encode},
    {"Cham_Code", decode_21, encode},
    {"Power Smart", decode_22, encode},
    {"Marantec", decode_23, encode},
    {"BETT", decode_24, encode},
    {"Doitrand", decode_25, encode},
    {"Phoenix_V2", decode_26, encode},
    {"Honeywell Sec", decode_27, encode},
    {"Honeywell", decode_28, encode},
    {"Magellan", decode_29, encode},
    {"Intertechno_V3", decode_30, encode},
    {"Clemsa", decode_31, encode},
    {"Ansonic", decode_32, encode},
    {"SMC5326", decode_33, encode},
    {"Holtek_HT12X", decode_34, encode},
    {"LinearDelta3", decode_35, encode},
    {"Dooya", decode_36, encode},
    {"Alutech AT-4N", decode_37, encode},
    {"KingGates Stylo4k", decode_38, encode},
    {"BinRAW", decode_39, encode},
    {"Mastercode", decode_40, encode},
    {"X10", decode_41, NULL},
    {"Hormann BiSecur", decode_42, encode},
    {"Legrand", decode_43, encode},
    {"Dickert_MAHS", decode_44, encode},
    {"GangQi", decode_45, encode},
    {"Marantec24", decode_46, encode},
    {"Hollarm", decode_47, encode},
    {"Hay21", decode_48, encode},
    {"Revers_RB2", decode_49, encode},
    {"Feron", decode_50, encode},
    {"Roger", decode_51, encode},
    {"Elplast", decode_52, encode},
    {"Treadmill37", decode_53, encode},
    {"Beninca ARC", decode_54, encode},
    {"Jarolift", decode_55, encode},
    {"Ditec GOL4", decode_56, encode},
    {"Doorbell32", decode_57, encode},
    {"KeyFinder", decode_58, encode},
    {"KeyFinder2", decode_59, encode},
    {"Nord ICE", decode_60, encode},
    {"Allstar Firefly", decode_61, encode},
    {"Telcoma/Cardin EDGE", decode_62, encode},
    {"Cardin S508", decode_63, NULL},
    {"Freezer", decode_64, encode},
    {"Faac TM", decode_65, encode},
    {"Proteco", decode_66, encode},
    {"BelKorn", decode_67, encode},
};
#define REGISTERED_PROTOCOLS_COUNT (sizeof(REGISTERED_PROTOCOLS) / sizeof(REGISTERED_PROTOCOLS[0]))

void subghz_protocol_registry_init(void) {}

bool subghz_protocol_registry_decode_all(const int32_t *pulses,
                                         size_t count,
                                         subghz_data_t *out_data) {
  return decode_named(NULL, pulses, count, out_data);
}

const subghz_protocol_t *subghz_protocol_registry_get_by_name(const char *name) {
  if (name == NULL)
    return NULL;
  for (size_t i = 0; i < REGISTERED_PROTOCOLS_COUNT; ++i) {
    if (strcmp(name, REGISTERED_PROTOCOLS[i].name) == 0)
      return &REGISTERED_PROTOCOLS[i];
  }
  return NULL;
}

size_t subghz_protocol_registry_encode(const char *name,
                                       const subghz_data_t *data,
                                       int32_t *out,
                                       size_t capacity) {
  const subghz_protocol_t *protocol = subghz_protocol_registry_get_by_name(name);
  if (data == NULL || protocol == NULL || protocol->encode == NULL || data->protocol_name == NULL ||
      strcmp(name, data->protocol_name) != 0)
    return 0;
  return protocol->encode(data, out, capacity);
}

bool subghz_protocol_registry_selftest(char *report, size_t report_len) {
  int32_t *pulses =
      heap_caps_calloc(SELFTEST_PULSES, sizeof(*pulses), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  subghz_data_t *data = heap_caps_calloc(1, sizeof(*data), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  bool passed = false;
  if (pulses == NULL || data == NULL) {
    ESP_LOGE(TAG, "Cannot allocate self-test workspace");
    goto cleanup;
  }
  data->protocol_name = "Princeton";
  data->bit_count = 24;
  data->raw_value = 0x108FA1;
  size_t count = encode(data, pulses, SELFTEST_PULSES / SELFTEST_REPEATS);
  if (count == 0)
    goto cleanup;
  for (size_t i = 1; i < SELFTEST_REPEATS; ++i)
    memcpy(pulses + count * i, pulses, count * sizeof(*pulses));
  passed = decode_named("Princeton", pulses, count * SELFTEST_REPEATS, data) &&
           data->raw_value == 0x108FA1 && data->bit_count == 24;
cleanup:
  free(data);
  free(pulses);
  if (report != NULL && report_len != 0)
    snprintf(report, report_len, "Princeton codec round-trip: %s", passed ? "PASS" : "FAIL");
  return passed;
}
