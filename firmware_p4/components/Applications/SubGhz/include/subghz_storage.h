#ifndef SUBGHZ_STORAGE_H
#define SUBGHZ_STORAGE_H

#include "subghz_types.h"
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Initialize the storage manager placeholder
 */
void subghz_storage_init(void);

/**
 * @brief Save a decoded signal to storage (Placeholder API)
 * @param name Suggested filename
 * @param data Decoded signal data
 * @param frequency Frequency in Hz
 * @param te Estimated TE in microseconds
 */
void subghz_storage_save_decoded(const char* name, const subghz_data_t* data, uint32_t frequency, uint32_t te);

/**
 * @brief Save a RAW signal to storage (Placeholder API)
 * @param name Suggested filename
 * @param pulses Array of signed timings
 * @param count Number of pulses
 * @param frequency Frequency in Hz
 */
void subghz_storage_save_raw(const char* name, const int32_t* pulses, size_t count, uint32_t frequency);

#endif // SUBGHZ_STORAGE_H
