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

#define LIFT_SHORT 400
#define LIFT_LONG  800
#define LIFT_TOL   40 // % Tolerance

static bool protocol_liftmaster_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 20) return false;

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

        if (subghz_check_pulse(pulse, LIFT_SHORT, LIFT_TOL) && 
            subghz_check_pulse(gap, LIFT_LONG, LIFT_TOL)) {
            bit = 0;
        } else if (subghz_check_pulse(pulse, LIFT_LONG, LIFT_TOL) && 
                   subghz_check_pulse(gap, LIFT_SHORT, LIFT_TOL)) {
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
            
            if (bits_found == 12) {
                out_data->protocol_name = "Liftmaster";
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

subghz_protocol_t protocol_liftmaster = {
    .name = "Liftmaster",
    .decode = protocol_liftmaster_decode,
    .encode = NULL
};
