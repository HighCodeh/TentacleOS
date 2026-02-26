#ifndef SUBGHZ_PROTOCOL_DECODER_H
#define SUBGHZ_PROTOCOL_DECODER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "subghz_types.h"

/**
 * @brief Interface for SubGhz Protocol Decoders
 */
typedef struct {
    const char* name;
    
    /**
     * @brief Decode raw pulse data into structured subghz_data_t
     * @param pulses Array of signed integers (positive=High, negative=Low in microseconds)
     * @param count Number of pulses in the array
     * @param out_data Pointer to store decoded results
     * @return true if signal was recognized and decoded
     */
    bool (*decode)(const int32_t* pulses, size_t count, subghz_data_t* out_data);
    
    /**
     * @brief Encode structured data back into pulse timings (for transmission)
     * @param data Data to encode
     * @param pulses Output buffer for pulse timings
     * @param max_count Size of the pulses buffer
     * @return Number of pulses written, or 0 on failure
     */
    size_t (*encode)(const subghz_data_t* data, int32_t* pulses, size_t max_count);
} subghz_protocol_t;

#endif // SUBGHZ_PROTOCOL_DECODER_H
