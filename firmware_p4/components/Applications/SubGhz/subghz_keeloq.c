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

#include "subghz_keeloq.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"

#include "storage_assets.h"

static const char *TAG = "SUBGHZ_KEELOQ";

#define KL_NLF          0x3A5C742EUL
#define kl_bit(x, n)    (((x) >> (n)) & 1)
#define kl_g5(x, a, b, c, d, e)                                                                    \
  (kl_bit(x, a) + kl_bit(x, b) * 2 + kl_bit(x, c) * 4 + kl_bit(x, d) * 8 + kl_bit(x, e) * 16)

#define KL_MFCODES_PATH "config/subghz/keeloq_mfcodes.txt"
#define KL_MAX_KEYS     160

// HCS301 line coding: PWM, bit 1 = long mark + short space, bit 0 = short mark +
// long space. Preamble of alternating Te pulses, then a header guard low.
#define KL_TE_SHORT      400
#define KL_TE_LONG       800
#define KL_PREAMBLE      12
#define KL_HEADER_LOW    (10 * KL_TE_SHORT)
#define KL_DATA_BITS     64
#define KL_HEADER_THRESH (5 * KL_TE_SHORT)
#define KL_DISC_MASK     0xFF // low 8 bits of the fixed part (HCS200/HCS300 common)

static keeloq_mfkey_t s_mfkeys[KL_MAX_KEYS];
static int s_mfkey_count = 0;

uint32_t subghz_keeloq_encrypt(uint32_t x, uint64_t key) {
  for (uint32_t r = 0; r < 528; r++) {
    x = (x >> 1) ^ (((uint32_t)(kl_bit(x, 0) ^ kl_bit(x, 16) ^ (uint32_t)kl_bit(key, r & 63) ^
                                kl_bit(KL_NLF, kl_g5(x, 1, 9, 20, 26, 31))))
                    << 31);
  }
  return x;
}

uint32_t subghz_keeloq_decrypt(uint32_t x, uint64_t key) {
  for (uint32_t r = 0; r < 528; r++) {
    x = (x << 1) ^ kl_bit(x, 31) ^ kl_bit(x, 15) ^ (uint32_t)kl_bit(key, (15 - r) & 63) ^
        kl_bit(KL_NLF, kl_g5(x, 0, 8, 19, 25, 30));
  }
  return x;
}

// Key-derivation schemes ported from Momentum firmware (GPLv3),
// lib/subghz/protocols/keeloq_common.c. `fix` is the 32-bit fixed part
// (button<<28 | serial); each scheme masks it as it needs.

static uint32_t word_rotate16(uint32_t v) {
  return (v >> 16) | (v << 16);
}

static uint64_t normal_learning(uint32_t fix, uint64_t mkey) {
  uint32_t k1 = subghz_keeloq_decrypt((fix & 0x0FFFFFFF) | 0x20000000, mkey);
  uint32_t k2 = subghz_keeloq_decrypt((fix & 0x0FFFFFFF) | 0x60000000, mkey);
  return ((uint64_t)k2 << 32) | k1;
}

static uint64_t secure_learning(uint32_t fix, uint32_t seed, uint64_t mkey) {
  uint32_t k1 = subghz_keeloq_decrypt(fix & 0x0FFFFFFF, mkey);
  uint32_t k2 = subghz_keeloq_decrypt(seed, mkey);
  return ((uint64_t)k1 << 32) | k2;
}

static uint64_t magic_xor_type1_learning(uint32_t fix, uint64_t xor_key) {
  uint32_t d = fix & 0x0FFFFFFF;
  return (((uint64_t)d << 32) | d) ^ xor_key;
}

static uint64_t magic_serial_type1_learning(uint32_t fix, uint64_t man) {
  return (man & 0xFFFFFFFF) | ((uint64_t)fix << 40) |
         ((uint64_t)(((fix & 0xFF) + ((fix >> 8) & 0xFF)) & 0xFF) << 32);
}

static uint64_t magic_serial_type2_learning(uint32_t fix, uint64_t man) {
  uint64_t hi = ((uint64_t)(fix & 0xFF) << 56) | ((uint64_t)((fix >> 8) & 0xFF) << 48) |
                ((uint64_t)((fix >> 16) & 0xFF) << 40) | ((uint64_t)((fix >> 24) & 0xFF) << 32);
  return (man & 0x00000000FFFFFFFFULL) | hi;
}

static uint64_t magic_serial_type3_learning(uint32_t fix, uint64_t man) {
  return (man & 0xFFFFFFFFFF000000ULL) | (fix & 0xFFFFFF);
}

static uint64_t pujol_learning(uint32_t fix, uint64_t mkey) {
  uint32_t d = fix & 0x0FFFFFFF;
  uint32_t w1 = subghz_keeloq_decrypt(d | 0x20000000, mkey);
  uint32_t w2 = subghz_keeloq_decrypt(d | 0x60000000, mkey);
  return ((uint64_t)word_rotate16(w2) << 32) | word_rotate16(w1);
}

// AERF uses a KeeLoq NLFSR variant for both key extension and decrypt.
static uint32_t nl_extend(uint32_t x, uint32_t k_lo, uint32_t k_hi, uint32_t outer_limit) {
  uint32_t r5 = 0;
  const uint32_t r6 = KL_NLF;
  while (r5 != outer_limit) {
    if (r5 < 0x210u) {
      uint32_t r1 = (x >> 15) & 1u;
      uint32_t r7 = r1 ^ ((x >> 1) | (x << 31));
      r1 = (15u - r5) & 0x3Fu;
      uint32_t lr = 32u - r1;
      uint32_t ip = r1 - 32u;
      lr = k_hi << lr;
      r1 = k_lo >> r1;
      ip = (r1 < 32u) ? (k_hi >> ip) : 0u;
      r1 = (r1 | lr | ip) & 1u;
      ip = (x >> 30) & 1u;
      r1 ^= r7;
      r7 = (x >> 25) & 1u;
      r7 += ip << 1;
      ip = (x >> 19) & 1u;
      ip += r7 << 1;
      r7 = (x >> 8) & 1u;
      r7 += ip << 1;
      x &= 1u;
      x += r7 << 1;
      x = (uint32_t)((int32_t)r6 >> (x & 31u));
      x &= 1u;
      x ^= r1;
    }
    r5 += 1u;
  }
  return x;
}

static uint32_t decrypt_derived(uint32_t hop, uint64_t man, uint32_t outer_limit) {
  return nl_extend(hop, (uint32_t)man, (uint32_t)(man >> 32), outer_limit);
}

static uint64_t aerf_learning(uint32_t fix, uint64_t key) {
  uint32_t k_lo = (uint32_t)key;
  uint32_t k_hi = (uint32_t)(key >> 32);
  uint32_t d = fix & 0x0FFFFFFF;
  uint32_t k1 = nl_extend(d | 0x20000000u, k_lo, k_hi, 0x40u);
  uint32_t k2 = nl_extend(d | 0x60000000u, k_lo, k_hi, 0x40u);
  return ((uint64_t)k2 << 32) | k1;
}

uint64_t subghz_keeloq_learn(uint64_t mkey, uint32_t fix, uint8_t type, uint32_t seed) {
  switch (type) {
    case KL_LEARN_SIMPLE:
    case KL_LEARN_SIMPLE_KINGGATES:
    case KL_LEARN_SIMPLE_JCM:
      return mkey;
    case KL_LEARN_SECURE:
      return secure_learning(fix, seed, mkey);
    case KL_LEARN_MAGIC_XOR_TYPE_1:
      return magic_xor_type1_learning(fix, mkey);
    case KL_LEARN_MAGIC_SERIAL_TYPE_1:
      return magic_serial_type1_learning(fix, mkey);
    case KL_LEARN_MAGIC_SERIAL_TYPE_2:
      return magic_serial_type2_learning(fix, mkey);
    case KL_LEARN_MAGIC_SERIAL_TYPE_3:
      return magic_serial_type3_learning(fix, mkey);
    case KL_LEARN_PUJOL:
      return pujol_learning(fix, mkey);
    case KL_LEARN_AERF:
      return aerf_learning(fix, mkey);
    case KL_LEARN_NORMAL:
    case KL_LEARN_NORMAL_JAROLIFT:
    default:
      // FAAC, Erreka and the make-specific Jarolift/Kinggates need a seed or a
      // separate protocol not yet ported; they fall back to normal learning.
      return normal_learning(fix, mkey);
  }
}

static void copy_trimmed(char *dst, size_t dst_size, const char *src) {
  while (*src == ' ' || *src == '\t') {
    src++;
  }
  size_t i = 0;
  while (src[i] != '\0' && src[i] != '\r' && src[i] != '\n' && i < dst_size - 1) {
    dst[i] = src[i];
    i++;
  }
  while (i > 0 && (dst[i - 1] == ' ' || dst[i - 1] == '\t')) {
    i--;
  }
  dst[i] = '\0';
}

int subghz_keeloq_load_mfkeys(void) {
  if (s_mfkey_count > 0) {
    return s_mfkey_count;
  }

  size_t sz = 0;
  if (storage_assets_get_file_size(KL_MFCODES_PATH, &sz) != ESP_OK || sz == 0) {
    ESP_LOGW(TAG, "mfcodes asset missing (%s)", KL_MFCODES_PATH);
    return -1;
  }
  char *buf = malloc(sz + 1);
  if (buf == NULL) {
    return -1;
  }
  size_t rd = 0;
  if (storage_assets_read_file(KL_MFCODES_PATH, (uint8_t *)buf, sz, &rd) != ESP_OK) {
    free(buf);
    return -1;
  }
  buf[rd] = '\0';

  char name[28] = "";
  uint64_t key = 0;
  bool have_name = false, have_key = false;

  char *save = NULL;
  for (char *line = strtok_r(buf, "\n", &save); line != NULL && s_mfkey_count < KL_MAX_KEYS;
       line = strtok_r(NULL, "\n", &save)) {
    char *p;
    if (strncmp(line, "Manufacturer:", 13) == 0) {
      copy_trimmed(name, sizeof(name), line + 13);
      have_name = name[0] != '\0';
    } else if ((p = strstr(line, "Key (Hex):")) != NULL) {
      key = strtoull(p + strlen("Key (Hex):"), NULL, 16);
      have_key = true;
    } else if ((p = strstr(line, "Type:")) != NULL) {
      int type = atoi(p + strlen("Type:"));
      if (have_name && have_key) {
        keeloq_mfkey_t *e = &s_mfkeys[s_mfkey_count++];
        snprintf(e->name, sizeof(e->name), "%s", name);
        e->key = key;
        e->type = (uint8_t)type;
      }
      have_name = have_key = false;
    }
  }

  free(buf);
  ESP_LOGI(TAG, "loaded %d KeeLoq manufacturer keys", s_mfkey_count);
  return s_mfkey_count;
}

int subghz_keeloq_mfkey_count(void) {
  return s_mfkey_count;
}

const keeloq_mfkey_t *subghz_keeloq_mfkey_at(int index) {
  if (index < 0 || index >= s_mfkey_count) {
    return NULL;
  }
  return &s_mfkeys[index];
}

static void emit_word(uint32_t word, int32_t *pulses, size_t *idx) {
  for (int b = 0; b < 32; b++) {
    if ((word >> b) & 1u) {
      pulses[(*idx)++] = KL_TE_LONG;
      pulses[(*idx)++] = -KL_TE_SHORT;
    } else {
      pulses[(*idx)++] = KL_TE_SHORT;
      pulses[(*idx)++] = -KL_TE_LONG;
    }
  }
}

static size_t pwm_encode_frame(uint32_t hop, uint32_t fix, int32_t *pulses, size_t max_count) {
  size_t need = (size_t)KL_PREAMBLE * 2 + 2 + KL_DATA_BITS * 2;
  if (pulses == NULL || max_count < need) {
    return 0;
  }
  size_t idx = 0;
  for (int i = 0; i < KL_PREAMBLE; i++) {
    pulses[idx++] = KL_TE_SHORT;
    pulses[idx++] = -KL_TE_SHORT;
  }
  pulses[idx++] = KL_TE_SHORT;
  pulses[idx++] = -KL_HEADER_LOW;
  emit_word(hop, pulses, &idx);
  emit_word(fix, pulses, &idx);
  return idx;
}

bool subghz_keeloq_pwm_decode(const int32_t *pulses,
                              size_t count,
                              uint32_t *out_fix,
                              uint32_t *out_hop) {
  // Find the header guard low, then the data starts on the next mark.
  size_t data = 0;
  bool found = false;
  for (size_t i = 0; i + 1 < count; i++) {
    if (pulses[i] < -KL_HEADER_THRESH) {
      data = i + 1;
      found = true;
      break;
    }
  }
  if (!found || data + (size_t)KL_DATA_BITS * 2 > count) {
    return false;
  }

  uint32_t words[2] = {0, 0};
  size_t k = data;
  for (int bit = 0; bit < KL_DATA_BITS; bit++) {
    int32_t mark = pulses[k];
    int32_t space = pulses[k + 1];
    if (mark <= 0 || space >= 0) {
      return false;
    }
    uint32_t b = ((uint32_t)mark > (uint32_t)(-space)) ? 1u : 0u;
    words[bit / 32] |= (b << (bit % 32));
    k += 2;
  }
  *out_hop = words[0];
  *out_fix = words[1];
  return true;
}

static bool keeloq_check(uint32_t plain, uint32_t serial, uint8_t fix_btn) {
  return ((plain >> 16) & KL_DISC_MASK) == (serial & KL_DISC_MASK) &&
         ((plain >> 28) & 0xF) == fix_btn;
}

static uint64_t byte_reverse64(uint64_t k) {
  uint64_t r = 0;
  for (int i = 0; i < 64; i += 8) {
    r |= (uint64_t)((uint8_t)(k >> i)) << (56 - i);
  }
  return r;
}

// Try the candidate plaintexts a manufacturer's learning type can produce; on the
// first that passes the discrimination + button check, return it. Mirrors the
// per-type decode in Momentum keeloq.c (UNKNOWN brute-tries several schemes;
// SECURE tries a serial-derived then a zero seed; AERF tries all decrypt limits).
static bool keeloq_recover(uint64_t mkey,
                           uint8_t type,
                           uint32_t fix,
                           uint32_t hop,
                           uint32_t serial,
                           uint8_t fix_btn,
                           uint32_t *out_plain) {
  uint32_t p;
  switch (type) {
    case KL_LEARN_AERF: {
      uint64_t man = aerf_learning(fix, mkey);
      p = decrypt_derived(hop, man, 0x240u);
      if (keeloq_check(p, serial, fix_btn)) break;
      p = decrypt_derived(hop, man, 0x210u);
      if (keeloq_check(p, serial, fix_btn)) break;
      p = subghz_keeloq_decrypt(hop, man);
      if (keeloq_check(p, serial, fix_btn)) break;
      return false;
    }
    case KL_LEARN_SECURE:
      p = subghz_keeloq_decrypt(hop, secure_learning(fix, fix & 0x0FFFFFFF, mkey));
      if (keeloq_check(p, serial, fix_btn)) break;
      p = subghz_keeloq_decrypt(hop, secure_learning(fix, 0, mkey));
      if (keeloq_check(p, serial, fix_btn)) break;
      return false;
    case KL_LEARN_UNKNOWN: {
      uint64_t rev = byte_reverse64(mkey);
      p = subghz_keeloq_decrypt(hop, mkey);
      if (keeloq_check(p, serial, fix_btn)) break;
      p = subghz_keeloq_decrypt(hop, rev);
      if (keeloq_check(p, serial, fix_btn)) break;
      p = subghz_keeloq_decrypt(hop, normal_learning(fix, mkey));
      if (keeloq_check(p, serial, fix_btn)) break;
      p = subghz_keeloq_decrypt(hop, normal_learning(fix, rev));
      if (keeloq_check(p, serial, fix_btn)) break;
      return false;
    }
    default:
      p = subghz_keeloq_decrypt(hop, subghz_keeloq_learn(mkey, fix, type, 0));
      if (keeloq_check(p, serial, fix_btn)) break;
      return false;
  }
  *out_plain = p;
  return true;
}

bool subghz_keeloq_identify(uint32_t fix, uint32_t hop, keeloq_result_t *out) {
  uint32_t serial = fix & 0x0FFFFFFF;
  uint8_t fix_btn = (fix >> 28) & 0xF;

  for (int i = 0; i < s_mfkey_count; i++) {
    uint32_t plain;
    if (keeloq_recover(s_mfkeys[i].key, s_mfkeys[i].type, fix, hop, serial, fix_btn, &plain)) {
      out->serial = serial;
      out->button = fix_btn;
      out->counter = plain & 0xFFFF;
      snprintf(out->mfname, sizeof(out->mfname), "%s", s_mfkeys[i].name);
      out->decrypted = true;
      return true;
    }
  }
  out->mfname[0] = '\0';
  out->decrypted = false;
  return false;
}

uint64_t subghz_keeloq_reverse64(uint64_t v) {
  uint64_t r = 0;
  for (int i = 0; i < 64; i++) {
    r = (r << 1) | (v & 1);
    v >>= 1;
  }
  return r;
}

bool subghz_keeloq_decode_key(uint64_t key, keeloq_result_t *out) {
  uint64_t c = subghz_keeloq_reverse64(key);
  return subghz_keeloq_identify((uint32_t)(c >> 32), (uint32_t)c, out);
}

size_t subghz_keeloq_encode(uint32_t serial,
                            uint8_t button,
                            uint16_t counter,
                            uint64_t mkey,
                            uint8_t type,
                            int32_t *pulses,
                            size_t max_count) {
  if (type == KL_LEARN_AERF) {
    return 0; // AERF uses the derived-key decrypt; no encrypt inverse to replay with
  }
  uint32_t fix = ((uint32_t)(button & 0xF) << 28) | (serial & 0x0FFFFFFF);
  uint64_t dkey = subghz_keeloq_learn(mkey, fix, type, 0);
  uint32_t disc = serial & KL_DISC_MASK;
  uint32_t plain = ((uint32_t)(button & 0xF) << 28) | (disc << 16) | counter;
  uint32_t hop = subghz_keeloq_encrypt(plain, dkey);
  // Transmit in on-air (Flipper .sub) order: reverse the cipher-order key.
  uint64_t logical = subghz_keeloq_reverse64(((uint64_t)fix << 32) | hop);
  return pwm_encode_frame((uint32_t)logical, (uint32_t)(logical >> 32), pulses, max_count);
}

bool subghz_keeloq_selftest(char *report, size_t report_len) {
  bool all_ok = true;
  size_t off = 0;
  if (report != NULL && report_len > 0) {
    report[0] = '\0';
  }
#define KL_REPORT(...)                                                                             \
  do {                                                                                            \
    if (report != NULL && report_len > off) {                                                     \
      off += snprintf(report + off, report_len - off, __VA_ARGS__);                                \
    }                                                                                             \
  } while (0)

  // 1) cipher is its own inverse.
  {
    uint64_t key = 0x0123456789ABCDEFULL;
    uint32_t pt = 0xDEADBEEF;
    bool ok = subghz_keeloq_decrypt(subghz_keeloq_encrypt(pt, key), key) == pt;
    all_ok &= ok;
    KL_REPORT("cipher inverse           %s\n", ok ? "PASS" : "FAIL");
  }

  // 2) full encode -> PWM -> demod -> decode_key round-trip (reversal + identify).
  if (s_mfkey_count > 0) {
    uint32_t serial = 0x1234567 & 0x0FFFFFFF;
    uint8_t btn = 0x2;
    uint16_t cnt = 0x000A;
    int32_t pulses[256];
    size_t n =
        subghz_keeloq_encode(serial, btn, cnt, 0x8455F43584941223ULL, KL_LEARN_SIMPLE, pulses, 256);
    uint32_t fix = 0, hop = 0;
    keeloq_result_t res = {0};
    bool ok = n > 0 && subghz_keeloq_pwm_decode(pulses, n, &fix, &hop) &&
              subghz_keeloq_decode_key(((uint64_t)fix << 32) | hop, &res) && res.counter == cnt &&
              res.serial == serial && res.button == btn;
    all_ok &= ok;
    KL_REPORT("encode/pwm/roundtrip     %s (%s cnt=%04X)\n", ok ? "PASS" : "FAIL", res.mfname,
              (unsigned)res.counter);
  }

  // 3) real captures from FlipperZero-Subghz-DB decode to the right maker + counter.
  if (s_mfkey_count > 0) {
    static const struct {
      const char *name;
      uint64_t key;
      uint16_t cnt;
    } vec[] = {
        {"DoorHan", 0x4850F07233789514ULL, 0x0004},
        {"Elmes_Poland", 0xDA8FF0433848ED44ULL, 0x0050},
        {"JCM_Tech", 0x3B5DBD838C154684ULL, 0x7578},
    };
    for (size_t v = 0; v < sizeof(vec) / sizeof(vec[0]); v++) {
      keeloq_result_t res = {0};
      bool ok = subghz_keeloq_decode_key(vec[v].key, &res) &&
                strcmp(res.mfname, vec[v].name) == 0 && res.counter == vec[v].cnt;
      all_ok &= ok;
      KL_REPORT("real %-12s %s (%s cnt=%04X)\n", vec[v].name, ok ? "PASS" : "FAIL", res.mfname,
                (unsigned)res.counter);
    }
  } else {
    KL_REPORT("identify                 SKIP (mfkeys not loaded)\n");
  }

#undef KL_REPORT
  return all_ok;
}
