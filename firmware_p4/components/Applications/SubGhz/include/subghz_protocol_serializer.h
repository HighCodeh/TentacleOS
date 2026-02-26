#ifndef SUBGHZ_PROTOCOL_SERIALIZER_H
#define SUBGHZ_PROTOCOL_SERIALIZER_H

#include "subghz_types.h"
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Get the preset ID based on current radio configuration
 * @return Preset ID (e.g., 6 for OOK 800kHz)
 */
uint8_t subghz_protocol_get_preset_id(void);

/**
 * @brief Serialize a decoded protocol signal to a string
 * @param data Decoded signal data
 * @param frequency Frequency in Hz
 * @param te Estimated TE in microseconds
 * @param out_buf Buffer to store the serialized string
 * @param out_size Size of the output buffer
 * @return size of string written
 */
size_t subghz_protocol_serialize_decoded(const subghz_data_t* data, uint32_t frequency, uint32_t te, char* out_buf, size_t out_size);

/**
 * @brief Serialize a RAW signal to a string
 * @param pulses Array of signed timings
 * @param count Number of pulses
 * @param frequency Frequency in Hz
 * @param out_buf Buffer to store the serialized string
 * @param out_size Size of the output buffer
 * @return size of string written
 */
size_t subghz_protocol_serialize_raw(const int32_t* pulses, size_t count, uint32_t frequency, char* out_buf, size_t out_size);

/**
 * @brief Parse a Bruce SubGhz string back into pulses and metadata
 * @param content The string content from a .sub file
 * @param out_pulses Buffer for decoded timings
 * @param max_count Max timings buffer size
 * @param out_frequency Pointer to store frequency
 * @param out_preset Pointer to store preset ID
 * @return number of pulses parsed
 */
size_t subghz_protocol_parse_raw(const char* content, int32_t* out_pulses, size_t max_count, uint32_t* out_frequency, uint8_t* out_preset);

#endif // SUBGHZ_PROTOCOL_SERIALIZER_H
