// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "subghz_protocol_decoder.h"
#include "subghz_protocol_utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * Nice Flo 12bit Protocol Implementation
 */

#define NICE_SHORT 700
#define NICE_LONG  1400
#define NICE_TOL   60 // % Tolerance

static bool protocol_nice_flo_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 24) return false; 

    for (size_t start_idx = 0; start_idx < count - 24; start_idx++) {
        uint32_t decoded_data = 0;
        int bits_found = 0;
        size_t k = start_idx;
        bool fail = false;

        while (k < count - 1 && bits_found < 12) {
            int32_t pulse = raw_data[k];
            int32_t gap   = raw_data[k+1];

            // Nice Flo: Pulse=+, Gap=-
            if (pulse <= 0 || gap >= 0) {
                fail = true;
                break;
            }

            // Bit 0: Short Pulse, Long Gap
            if (subghz_check_pulse(pulse, NICE_SHORT, NICE_TOL) && 
                subghz_check_pulse(gap, NICE_LONG, NICE_TOL)) {
                decoded_data = (decoded_data << 1) | 0;
                bits_found++;
            }
            // Bit 1: Long Pulse, Short Gap
            else if (subghz_check_pulse(pulse, NICE_LONG, NICE_TOL) && 
                     subghz_check_pulse(gap, NICE_SHORT, NICE_TOL)) {
                decoded_data = (decoded_data << 1) | 1;
                bits_found++;
            } else {
                fail = true;
                break;
            }
            k += 2;
        }

        if (!fail && bits_found == 12) {
            out_data->protocol_name = "Nice Flo 12bit";
            out_data->bit_count = 12;
            out_data->raw_value = decoded_data;
            // Common Nice Flo: 10 switches + 2 buttons
            out_data->serial = decoded_data >> 2;
            out_data->btn = decoded_data & 0x03;
            return true;
        }
    }

    return false;
}

subghz_protocol_t protocol_nice_flo = {
    .name = "Nice Flo",
    .decode = protocol_nice_flo_decode,
    .encode = NULL
};
