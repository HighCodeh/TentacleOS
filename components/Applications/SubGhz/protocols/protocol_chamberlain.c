#include "subghz_protocol_defs.h"
#include <string.h>

#define CHAM_SHORT 430
#define CHAM_LONG  870
#define CHAM_TOL   200

static bool is_within(int32_t val, int32_t target) {
    if (val < 0) val = -val;
    return (val >= target - CHAM_TOL) && (val <= target + CHAM_TOL);
}

static bool protocol_chamberlain_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 16) return false;

    uint32_t decoded_data = 0;
    int bits_found = 0;

    for (size_t i = 0; i < count - 1; i += 2) {
        int32_t pulse = raw_data[i];
        int32_t gap   = raw_data[i+1];

        if (pulse < 0) { if(i==0){i--; continue;} return false; }

        int bit = -1;

        // Bit 0: Short Pulse, Long Gap
        if (is_within(pulse, CHAM_SHORT) && is_within(gap, CHAM_LONG)) {
            bit = 0;
        }
        // Bit 1: Long Pulse, Short Gap
        else if (is_within(pulse, CHAM_LONG) && is_within(gap, CHAM_SHORT)) {
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
            
            // Chamberlain costuma ter 9 ou 8 bits?
            // Security+ 1.0 é diferente. Vamos assumir genérico ~9-12 bits
            if (bits_found >= 8 && bits_found <= 12) {
                 // Check if next is sync/stop?
                 // Simplification: Return true for now
                 if (bits_found == 9) { // Common length
                    out_data->protocol_name = "Chamberlain";
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

subghz_protocol_t protocol_chamberlain = {
    .name = "Chamberlain",
    .decode = protocol_chamberlain_decode,
    .encode = NULL
};
