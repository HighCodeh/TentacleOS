#include "subghz_protocol_decoder.h"
#include "subghz_protocol_utils.h"
#include <string.h>

#define LINEAR_SHORT 500
#define LINEAR_LONG  1500
#define LINEAR_TOL   40 // % Tolerance

static bool protocol_linear_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 10) return false;

    uint32_t decoded_data = 0;
    int bits_found = 0;

    for (size_t i = 0; i < count - 1; i += 2) {
        int32_t pulse = raw_data[i];
        int32_t gap   = raw_data[i+1];

        if (pulse < 0) { 
            if (i == 0) {
                i--; 
                continue;
            } 
            return false; 
        }

        int bit = -1;

        // Bit 0: Short, Long
        if (subghz_check_pulse(pulse, LINEAR_SHORT, LINEAR_TOL) && 
            subghz_check_pulse(gap, LINEAR_LONG, LINEAR_TOL)) {
            bit = 0;
        }
        // Bit 1: Long, Short
        else if (subghz_check_pulse(pulse, LINEAR_LONG, LINEAR_TOL) && 
                 subghz_check_pulse(gap, LINEAR_SHORT, LINEAR_TOL)) {
            bit = 1;
        }
        else {
            if (bits_found >= 8) break;
            decoded_data = 0;
            bits_found = 0;
            continue;
        }

        if (bit != -1) {
            decoded_data = (decoded_data << 1) | bit;
            bits_found++;
            
            if (bits_found == 10) {
                out_data->protocol_name = "Linear";
                out_data->bit_count = bits_found;
                out_data->raw_value = decoded_data;
                out_data->serial = decoded_data;
                out_data->btn = 0;
                return true;
            }
        }
    }
    return false;
}

subghz_protocol_t protocol_linear = {
    .name = "Linear",
    .decode = protocol_linear_decode,
    .encode = NULL
};
