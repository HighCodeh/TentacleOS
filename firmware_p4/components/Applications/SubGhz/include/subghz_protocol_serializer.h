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

#ifndef SUBGHZ_PROTOCOL_SERIALIZER_H
#define SUBGHZ_PROTOCOL_SERIALIZER_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "subghz_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the numeric identifier of the current radio preset.
 *
 * @return Preset identifier.
 */
uint8_t subghz_protocol_get_preset_id(void);

/**
 * @brief Serialize decoded signal data into a text buffer.
 *
 * @param data       Decoded signal data to serialize.
 * @param frequency  Center frequency in Hz.
 * @param te         Base time element in microseconds.
 * @param out_buf    Output buffer for the serialized string.
 * @param out_size   Size of the output buffer in bytes.
 * @return Number of bytes written (excluding null terminator), or 0 on failure.
 */
size_t subghz_protocol_serialize_decoded(
    const subghz_data_t *data, uint32_t frequency, uint32_t te, char *out_buf, size_t out_size);

/**
 * @brief Serialize raw pulse data into a text buffer.
 *
 * @param pulses     Array of signed pulse durations in microseconds.
 * @param count      Number of elements in the pulses array.
 * @param frequency  Center frequency in Hz.
 * @param out_buf    Output buffer for the serialized string.
 * @param out_size   Size of the output buffer in bytes.
 * @return Number of bytes written (excluding null terminator), or 0 on failure.
 */
size_t subghz_protocol_serialize_raw(
    const int32_t *pulses, size_t count, uint32_t frequency, char *out_buf, size_t out_size);

/**
 * @brief Parse a serialized raw signal back into pulse data.
 *
 * @param content        Serialized string to parse.
 * @param out_pulses     Output buffer for pulse durations.
 * @param max_count      Maximum number of pulses the buffer can hold.
 * @param out_frequency  Pointer to store the parsed frequency.
 * @param out_preset     Pointer to store the parsed preset identifier.
 * @return Number of pulses parsed, or 0 on failure.
 */
size_t subghz_protocol_parse_raw(const char *content,
                                 int32_t *out_pulses,
                                 size_t max_count,
                                 uint32_t *out_frequency,
                                 uint8_t *out_preset);

/** Parse a complete RAW or encodable decoded capture. Discard pulse output on error. */
esp_err_t subghz_protocol_parse_capture(const char *content,
                                        int32_t *out_pulses,
                                        size_t capacity,
                                        size_t *out_count,
                                        uint32_t *out_frequency,
                                        uint8_t *out_preset);

#ifdef __cplusplus
}
#endif

#endif // SUBGHZ_PROTOCOL_SERIALIZER_H
