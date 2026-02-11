#include "subghz_protocol_defs.h"
#include <string.h>

#define HOLTEK_SHORT 430
#define HOLTEK_LONG  870
#define HOLTEK_TOL   150

static bool is_within(int32_t val, int32_t target) {
    if (val < 0) val = -val;
    return (val >= target - HOLTEK_TOL) && (val <= target + HOLTEK_TOL);
}

static bool protocol_holtek_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 24) return false; // 12 bits = 24 transitions

    uint32_t decoded_data = 0;
    int bits_found = 0;

    for (size_t i = 0; i < count - 1; i += 2) {
        int32_t pulse = raw_data[i];
        int32_t gap   = raw_data[i+1];

        if (pulse < 0) { if(i==0){i--; continue;} return false; }

        int bit = -1;

        if (is_within(pulse, HOLTEK_SHORT) && is_within(gap, HOLTEK_LONG)) {
            bit = 0;
        } else if (is_within(pulse, HOLTEK_LONG) && is_within(gap, HOLTEK_SHORT)) {
            bit = 1;
        } else {
            decoded_data = 0;
            bits_found = 0;
            continue;
        }

        if (bit != -1) {
            decoded_data = (decoded_data << 1) | bit;
            bits_found++;
            
            if (bits_found == 12) { // HT12E standard
                out_data->protocol_name = "Holtek";
                out_data->bit_count = 12;
                out_data->raw_value = decoded_data;
                out_data->serial = decoded_data >> 4; // 8 addr + 4 data
                out_data->btn = decoded_data & 0x0F;
                return true;
            }
        }
    }
    return false;
}

subghz_protocol_t protocol_holtek = {
    .name = "Holtek",
    .decode = protocol_holtek_decode,
    .encode = NULL
};
