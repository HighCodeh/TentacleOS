#include "subghz_protocol_decoder.h"
#include "subghz_protocol_utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * Princeton / PT2262 Protocol Implementation (Alternate)
 */

#define PRINCETON_SHORT 350
#define PRINCETON_LONG  1050
#define PRINCETON_TOL   60 // % Tolerance

static bool protocol_princeton_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 24) return false;

    // We try to find bits directly. Princeton is PWM.
    for (size_t start_idx = 0; start_idx < count - 24; start_idx++) {
        uint32_t decoded_data = 0;
        int bits_found = 0;
        size_t k = start_idx;
        bool fail = false;

        while (k < count - 1 && bits_found < 24) {
            int32_t pulse = raw_data[k];
            int32_t gap   = raw_data[k+1];

            if (pulse <= 0 || gap >= 0) {
                fail = true;
                break;
            }

            // Test Zero: Short High, Long Low (1:3)
            if (subghz_check_pulse(pulse, PRINCETON_SHORT, PRINCETON_TOL) && 
                subghz_check_pulse(gap, PRINCETON_LONG, PRINCETON_TOL)) {
                decoded_data = (decoded_data << 1) | 0;
                bits_found++;
            }
            // Test One: Long High, Short Low (3:1)
            else if (subghz_check_pulse(pulse, PRINCETON_LONG, PRINCETON_TOL) && 
                     subghz_check_pulse(gap, PRINCETON_SHORT, PRINCETON_TOL)) {
                decoded_data = (decoded_data << 1) | 1;
                bits_found++;
            } else {
                fail = true;
                break;
            }
            k += 2;
        }

        if (!fail && bits_found >= 12) {
            out_data->protocol_name = "Princeton (Alt)";
            out_data->bit_count = bits_found;
            out_data->raw_value = decoded_data;
            out_data->serial = decoded_data >> 4;
            out_data->btn = decoded_data & 0x0F;
            return true;
        }
    }

    return false;
}

subghz_protocol_t protocol_princeton = {
    .name = "Princeton",
    .decode = protocol_princeton_decode,
    .encode = NULL
};
