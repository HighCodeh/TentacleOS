#include "subghz_protocol_defs.h"
#include <string.h>

#define LIFT_SHORT 400
#define LIFT_LONG  800
#define LIFT_TOL   200

static bool is_within(int32_t val, int32_t target) {
    if (val < 0) val = -val;
    return (val >= target - LIFT_TOL) && (val <= target + LIFT_TOL);
}

static bool protocol_liftmaster_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 20) return false;

    uint32_t decoded_data = 0;
    int bits_found = 0;

    for (size_t i = 0; i < count - 1; i += 2) {
        int32_t pulse = raw_data[i];
        int32_t gap   = raw_data[i+1];

        if (pulse < 0) { if(i==0){i--; continue;} return false; }

        int bit = -1;

        if (is_within(pulse, LIFT_SHORT) && is_within(gap, LIFT_LONG)) {
            bit = 0;
        } else if (is_within(pulse, LIFT_LONG) && is_within(gap, LIFT_SHORT)) {
            bit = 1;
        } else {
            decoded_data = 0;
            bits_found = 0;
            continue;
        }

        if (bit != -1) {
            decoded_data = (decoded_data << 1) | bit;
            bits_found++;
            
            if (bits_found > 10) {
                 // Liftmaster detected
                 if (bits_found == 12) { // Example
                    out_data->protocol_name = "Liftmaster";
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

subghz_protocol_t protocol_liftmaster = {
    .name = "Liftmaster",
    .decode = protocol_liftmaster_decode,
    .encode = NULL
};
