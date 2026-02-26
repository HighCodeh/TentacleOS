#include "subghz_protocol_decoder.h"
#include "subghz_protocol_utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * CAME 12bit / 24bit Protocol Implementation
 */

#define CAME_SHORT 320
#define CAME_LONG  640
#define CAME_TOL   60 // % Tolerance

static bool protocol_came_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 24) return false; 

    // CAME doesn't have a strict sync bit in the same way as RCSwitch, 
    // it's usually preceded by a long gap (pilot).
    // We try to find a sequence of bits.

    for (size_t start_idx = 0; start_idx < count - 24; start_idx++) {
        uint32_t decoded_data = 0;
        int bits_found = 0;
        size_t k = start_idx;

        while (k < count - 1 && bits_found < 24) {
            int32_t pulse = raw_data[k];
            int32_t gap   = raw_data[k+1];

            // CAME is Active High: Pulse=+, Gap=-
            if (pulse <= 0 || gap >= 0) {
                break;
            }

            // Bit 0: Short Pulse (1 Te), Long Gap (2 Te)
            if (subghz_check_pulse(pulse, CAME_SHORT, CAME_TOL) && 
                subghz_check_pulse(gap, CAME_LONG, CAME_TOL)) {
                decoded_data = (decoded_data << 1) | 0;
                bits_found++;
            }
            // Bit 1: Long Pulse (2 Te), Short Gap (1 Te)
            else if (subghz_check_pulse(pulse, CAME_LONG, CAME_TOL) && 
                     subghz_check_pulse(gap, CAME_SHORT, CAME_TOL)) {
                decoded_data = (decoded_data << 1) | 1;
                bits_found++;
            } else {
                break;
            }
            k += 2;

            // Check if we reached a valid bit count
            if (bits_found == 12 || bits_found == 24) {
                // If the next pulses don't match, we might have a valid packet
                // (CAME sends exactly 12 or 24)
                if (k >= count - 1 || (!subghz_check_pulse(raw_data[k], CAME_SHORT, CAME_TOL) && 
                                      !subghz_check_pulse(raw_data[k], CAME_LONG, CAME_TOL))) {
                    
                    out_data->protocol_name = (bits_found == 12) ? "CAME 12bit" : "CAME 24bit";
                    out_data->bit_count = bits_found;
                    out_data->raw_value = decoded_data;
                    out_data->serial = decoded_data;
                    out_data->btn = 0;
                    return true;
                }
            }
        }
    }

    return false;
}

static size_t protocol_came_encode(const subghz_data_t* data, int32_t* pulses, size_t max_count) {
    if (data->bit_count != 12 && data->bit_count != 24) return 0;
    if (max_count < (size_t)(data->bit_count * 2 + 1)) return 0;

    size_t idx = 0;
    
    // Pilot pulse/gap is usually handled by the sender or can be added here
    // For CAME, we'll just encode the bits.

    for (int i = data->bit_count - 1; i >= 0; i--) {
        bool bit = (data->raw_value >> i) & 1;
        if (bit) {
            // Bit 1: Long Pulse, Short Gap
            pulses[idx++] = CAME_LONG;
            pulses[idx++] = -CAME_SHORT;
        } else {
            // Bit 0: Short Pulse, Long Gap
            pulses[idx++] = CAME_SHORT;
            pulses[idx++] = -CAME_LONG;
        }
    }

    // Stop bit / Final gap
    pulses[idx++] = -CAME_SHORT * 20; // Long gap to separate packets

    return idx;
}

subghz_protocol_t protocol_came = {
    .name = "CAME",
    .decode = protocol_came_decode,
    .encode = protocol_came_encode
};
