#include "subghz_protocol_defs.h"
#include <string.h>

#define NICE_SHORT 700
#define NICE_LONG  1400
#define NICE_TOL   250

static bool is_within(int32_t val, int32_t target) {
    if (val < 0) val = -val;
    return (val >= target - NICE_TOL) && (val <= target + NICE_TOL);
}

static bool protocol_nice_flo_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 24) return false; 

    uint32_t decoded_data = 0;
    int bits_found = 0;

    for (size_t i = 0; i < count - 1; i += 2) {
        int32_t pulse = raw_data[i];
        int32_t gap   = raw_data[i+1];

        // Sincronia inicial (se necessário)
        // Nice Flo muitas vezes começa direto

        if (pulse < 0) { if (i==0) {i--; continue;} return false; }

        int bit = -1;

        // Bit 0: Short Pulse, Long Gap (Duty 1/3)
        if (is_within(pulse, NICE_SHORT) && is_within(gap, NICE_LONG)) {
            bit = 0;
        }
        // Bit 1: Long Pulse, Short Gap (Duty 2/3)
        else if (is_within(pulse, NICE_LONG) && is_within(gap, NICE_SHORT)) {
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
            
            if (bits_found == 12) {
                out_data->protocol_name = "Nice Flo 12bit";
                out_data->bit_count = 12;
                out_data->raw_value = decoded_data;
                // Nice Flo: 9 bits DIP switch + 2 bits btn? Ou 10+2?
                // Estrutura comum: 10 switches + 2 buttons
                out_data->serial = decoded_data >> 2;
                out_data->btn = decoded_data & 0x03;
                return true;
            }
        }
    }

    return false;
}

subghz_protocol_t protocol_nice_flo = {
    .name = "Nice Flo",
    .decode = protocol_nice_flo_decode,
    .encode = NULL // TODO
};
