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

#ifndef SUBGHZ_KEELOQ_H
#define SUBGHZ_KEELOQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Manufacturer key-derivation scheme (numbers mirror the Flipper/Momentum
 *         mfcodes "Type" field). The deterministic schemes (derivable from the
 *         fixed part alone: SIMPLE, NORMAL, MAGIC_XOR, MAGIC_SERIAL_1/2/3, PUJOL,
 *         AERF, SIMPLE_JCM) are ported and cover ~103 of the 116 keys. The
 *         seed/mix-based ones (SECURE, FAAC, ERREKA) and the make-specific
 *         KINGGATES/JAROLIFT fall back to NORMAL until the seed is extracted from
 *         the transmission. Derivations ported from Momentum firmware (GPLv3),
 *         lib/subghz/protocols/keeloq_common.c. */
typedef enum {
  KL_LEARN_UNKNOWN = 0,
  KL_LEARN_SIMPLE = 1, ///< device key == manufacturer key
  KL_LEARN_NORMAL = 2, ///< Microchip normal learning (from the serial)
  KL_LEARN_SECURE = 3, ///< normal + a 32-bit seed (needs the seed from the frame)
  KL_LEARN_MAGIC_XOR_TYPE_1 = 4,
  KL_LEARN_FAAC = 5, ///< seed-based (not yet ported)
  KL_LEARN_MAGIC_SERIAL_TYPE_1 = 6,
  KL_LEARN_MAGIC_SERIAL_TYPE_2 = 7,
  KL_LEARN_MAGIC_SERIAL_TYPE_3 = 8,
  KL_LEARN_SIMPLE_KINGGATES = 10,
  KL_LEARN_NORMAL_JAROLIFT = 11, ///< separate protocol upstream (falls back)
  KL_LEARN_ERREKA = 12,          ///< seed/mix-based (not yet ported)
  KL_LEARN_PUJOL = 13,
  KL_LEARN_AERF = 14, ///< uses the derived-key decrypt variant
  KL_LEARN_SIMPLE_JCM = 15,
} keeloq_learning_t;

/** @brief One manufacturer entry from the mfcodes asset. */
typedef struct {
  char name[28];
  uint64_t key;
  uint8_t type;
} keeloq_mfkey_t;

/** @brief A decoded / to-be-encoded HCS301 transmission. */
typedef struct {
  uint32_t serial;   ///< 28-bit device serial
  uint8_t button;    ///< 4-bit button code
  uint16_t counter;  ///< 16-bit sync counter
  char mfname[28];   ///< matched manufacturer name, "" if not identified
  bool decrypted;    ///< true if a key produced a valid frame
} keeloq_result_t;

/** @brief KeeLoq block cipher (Microchip, 528 rounds). Encrypt is its own
 *         inverse under decrypt. Exposed for tests and the encode path. */
uint32_t subghz_keeloq_encrypt(uint32_t data, uint64_t key);
uint32_t subghz_keeloq_decrypt(uint32_t data, uint64_t key);

/** @brief Derive the per-device key from a manufacturer key. @p seed is used only
 *         by SECURE learning (0 otherwise). */
uint64_t subghz_keeloq_learn(uint64_t mkey, uint32_t serial, uint8_t type, uint32_t seed);

/** @brief Load manufacturer keys from config/subghz/keeloq_mfcodes.txt on the SD
 *         assets volume. Idempotent; safe to call repeatedly.
 *  @return number of keys loaded, or a negative error. */
int subghz_keeloq_load_mfkeys(void);
int subghz_keeloq_mfkey_count(void);
const keeloq_mfkey_t *subghz_keeloq_mfkey_at(int index);

/** @brief Try every loaded manufacturer key against a captured frame (fixed part
 *         + encrypted hop) and, on a discrimination match, fill @p out with the
 *         recovered serial/button/counter and the manufacturer name.
 *  @return true if a key claimed the frame. */
bool subghz_keeloq_identify(uint32_t fix, uint32_t hop, keeloq_result_t *out);

/** @brief Build the HCS301 pulse train (us, +mark/-space) for a rolling code:
 *         encrypts @p counter under the device key derived from @p mkey/@p type,
 *         packs the 64-bit frame and adds preamble + header. For a rolling-code
 *         attack the caller passes the captured counter + 1.
 *  @return pulse count, or 0 on error / small buffer. */
size_t subghz_keeloq_encode(uint32_t serial,
                            uint8_t button,
                            uint16_t counter,
                            uint64_t mkey,
                            uint8_t type,
                            int32_t *pulses,
                            size_t max_count);

/** @brief Demodulate an HCS301 pulse train back to the fixed part + hop.
 *  @return true if a 64-bit frame was recovered. */
bool subghz_keeloq_pwm_decode(const int32_t *pulses, size_t count, uint32_t *out_fix,
                              uint32_t *out_hop);

/** @brief Round-trip self-test (crypto + frame + PWM + identify). Needs the
 *         mfkeys loaded for the identify stage. Writes an optional report.
 *  @return true if every case passed. */
bool subghz_keeloq_selftest(char *report, size_t report_len);

#ifdef __cplusplus
}
#endif

#endif // SUBGHZ_KEELOQ_H
