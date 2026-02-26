#include "subghz_protocol_decoder.h"
#include "subghz_protocol_utils.h"
#include <string.h>

#define HOLTEK_SHORT 430
#define HOLTEK_LONG  870
#define HOLTEK_TOL   40 // % Tolerance

static bool protocol_holtek_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 24) return false; // 12 bits = 24 transitions

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

        if (subghz_check_pulse(pulse, HOLTEK_SHORT, HOLTEK_TOL) && 
            subghz_check_pulse(gap, HOLTEK_LONG, HOLTEK_TOL)) {
            bit = 0;
        } else if (subghz_check_pulse(pulse, HOLTEK_LONG, HOLTEK_TOL) && 
                   subghz_check_pulse(gap, HOLTEK_SHORT, HOLTEK_TOL)) {
            bit = 1;
        } else {
            if (bits_found >= 8) break;
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

