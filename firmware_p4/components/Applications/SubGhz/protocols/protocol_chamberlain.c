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

#define CHAM_SHORT 430
#define CHAM_LONG  870
#define CHAM_TOL   40 // % Tolerance

static bool protocol_chamberlain_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 16) return false;

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

        // Bit 0: Short Pulse, Long Gap
        if (subghz_check_pulse(pulse, CHAM_SHORT, CHAM_TOL) && 
            subghz_check_pulse(gap, CHAM_LONG, CHAM_TOL)) {
            bit = 0;
        }
        // Bit 1: Long Pulse, Short Gap
        else if (subghz_check_pulse(pulse, CHAM_LONG, CHAM_TOL) && 
                 subghz_check_pulse(gap, CHAM_SHORT, CHAM_TOL)) {
            bit = 1;
        }
        else {
            if (bits_found >= 7) break;
            decoded_data = 0;
            bits_found = 0;
            continue;
        }

        if (bit != -1) {
            decoded_data = (decoded_data << 1) | bit;
            bits_found++;
            
            // Chamberlain typically 7, 8 or 9 bits
            if (bits_found == 9) {
                out_data->protocol_name = "Chamberlain";
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

subghz_protocol_t protocol_chamberlain = {
    .name = "Chamberlain",
    .decode = protocol_chamberlain_decode,
    .encode = NULL
};
