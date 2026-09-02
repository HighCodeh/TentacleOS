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

#ifndef SUBGHZ_PROTOCOL_REGISTRY_H
#define SUBGHZ_PROTOCOL_REGISTRY_H

#include "subghz_protocol_decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the protocol registry.
 */
void subghz_protocol_registry_init(void);

/**
 * @brief Run all registered decoders on the signal.
 *
 * @param pulses    Array of signed pulse durations in microseconds.
 * @param count     Number of elements in the pulses array.
 * @param out_data  Pointer to store decoded results.
 * @return true if a protocol claimed the signal.
 */
bool subghz_protocol_registry_decode_all(const int32_t *pulses,
                                         size_t count,
                                         subghz_data_t *out_data);

/**
 * @brief Find a protocol by name.
 *
 * @param name  Protocol name to search for.
 * @return Pointer to protocol structure, or NULL if not found.
 */
const subghz_protocol_t *subghz_protocol_registry_get_by_name(const char *name);

/**
 * @brief Encode a decoded signal back to pulse timings via the named protocol's
 *        encoder. Resolves decorated names ("CAME 12bit", "RCSwitch(P1)").
 *
 * @param name       Protocol name (exact or decorated).
 * @param data       Decoded values to encode (bit_count, raw_value, name).
 * @param out        Output buffer for pulse timings (us, +mark/-space).
 * @param max_count  Size of @p out.
 * @return Pulse count, or 0 if the protocol is unknown or decode-only.
 */
size_t subghz_protocol_registry_encode(const char *name,
                                       const subghz_data_t *data,
                                       int32_t *out,
                                       size_t max_count);

/**
 * @brief Round-trip self-test of every protocol that has an encoder (today CAME
 *        and RCSwitch): encode a known value, decode it back, confirm it matches.
 *
 * @param report      Optional buffer for a human-readable per-case report.
 * @param report_len  Size of @p report (ignored if report is NULL).
 * @return true if every case passed.
 */
bool subghz_protocol_registry_selftest(char *report, size_t report_len);

#ifdef __cplusplus
}
#endif

#endif // SUBGHZ_PROTOCOL_REGISTRY_H
