#include "subghz_protocol_defs.h"
#include <string.h>

#define LINEAR_SHORT 500
#define LINEAR_LONG  1500
#define LINEAR_TOL   250

static bool is_within(int32_t val, int32_t target) {
    if (val < 0) val = -val;
    return (val >= target - LINEAR_TOL) && (val <= target + LINEAR_TOL);
}

static bool protocol_linear_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 10) return false;

    uint32_t decoded_data = 0;
    int bits_found = 0;

    for (size_t i = 0; i < count - 1; i += 2) {
        int32_t pulse = raw_data[i];
        int32_t gap   = raw_data[i+1];

        if (pulse < 0) { if(i==0){i--; continue;} return false; }

        int bit = -1;

        // Bit 0: Short, Long
        if (is_within(pulse, LINEAR_SHORT) && is_within(gap, LINEAR_LONG)) {
            bit = 0;
        }
        // Bit 1: Long, Short
        else if (is_within(pulse, LINEAR_LONG) && is_within(gap, LINEAR_SHORT)) {
            bit = 1;
        }
        else {
            decoded_data = 0;
            bits_found = 0;
            continue;
        }

        if (bit != -1) {
            decoded_data = (decoded_data << 1) | bit;
            bits_found++;
            
            // Linear MegaCode often 10+ bits or fixed length?
            // Assuming 10 for now based on common openers
            if (bits_found >= 10) { // Just valid stream detection
                 if (bits_found == 10) { // Try to catch small packets
                    out_data->protocol_name = "Linear";
                    out_data->bit_count = bits_found;
                    out_data->raw_value = decoded_data;
                    out_data->serial = decoded_data;
                    return true;
                 }
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
