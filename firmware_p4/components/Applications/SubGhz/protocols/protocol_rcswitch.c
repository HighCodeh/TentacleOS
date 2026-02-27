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
 * RCSwitch Original Protocol Implementation
 * Ported to TentacleOS for improved accuracy using derived timings.
 */

typedef struct {
    uint16_t pulseLength;
    struct { uint8_t high; uint8_t low; } syncFactor;
    struct { uint8_t high; uint8_t low; } zero;
    struct { uint8_t high; uint8_t low; } one;
    bool invertedSignal;
} rcswitch_proto_t;

static const rcswitch_proto_t proto[] = {
    { 350, {  1, 31 }, {  1,  3 }, {  3,  1 }, false },    // protocol 1
    { 650, {  1, 10 }, {  1,  2 }, {  2,  1 }, false },    // protocol 2
    { 100, { 30, 71 }, {  4, 11 }, {  9,  6 }, false },    // protocol 3
    { 380, {  1,  6 }, {  1,  3 }, {  3,  1 }, false },    // protocol 4
    { 500, {  6, 14 }, {  1,  2 }, {  2,  1 }, false },    // protocol 5
    { 450, { 23,  1 }, {  1,  2 }, {  2,  1 }, true },     // protocol 6 (HT6P20B)
    { 150, {  2, 62 }, {  1,  6 }, {  6,  1 }, false },    // protocol 7 (HS2303-PT)
    { 200, {  3, 130}, {  7, 16 }, {  3,  16}, false},     // protocol 8 Conrad RS-200 RX
    { 200, { 130, 7 }, {  16, 7 }, { 16,  3 }, true},      // protocol 9 Conrad RS-200 TX
    { 365, { 18,  1 }, {  3,  1 }, {  1,  3 }, true },     // protocol 10 (1ByOne Doorbell)
    { 270, { 36,  1 }, {  1,  2 }, {  2,  1 }, true },     // protocol 11 (HT12E)
    { 320, { 36,  1 }, {  1,  2 }, {  2,  1 }, true }      // protocol 12 (SM5212)
};

#define NUM_PROTO (sizeof(proto) / sizeof(proto[0]))
#define RECEIVE_TOLERANCE 60 // 60% as per original RCSwitch

static bool protocol_rcswitch_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 10) return false;

    for (int p = 0; p < NUM_PROTO; p++) {
        const rcswitch_proto_t* pro = &proto[p];
        
        for (size_t i = 0; i < count - 1; i++) {
            bool first_is_high = (raw_data[i] > 0);
            bool second_is_high = (raw_data[i+1] > 0);
            
            if (pro->invertedSignal) {
                if (first_is_high || !second_is_high) continue; 
            } else {
                if (!first_is_high || second_is_high) continue;
            }

            uint32_t dur1 = abs(raw_data[i]);
            uint32_t dur2 = abs(raw_data[i+1]);

            uint32_t sync_long_factor = (pro->syncFactor.low > pro->syncFactor.high) ? pro->syncFactor.low : pro->syncFactor.high;
            uint32_t dur_long = (pro->syncFactor.low > pro->syncFactor.high) ? dur2 : dur1;
            
            if (sync_long_factor == 0) continue;
            uint32_t delay = dur_long / sync_long_factor;
            if (delay < 50 || delay > 1200) continue; 

            // Validate entire Sync Bit using unified pulse checker
            if (!subghz_check_pulse(raw_data[i], delay * pro->syncFactor.high, RECEIVE_TOLERANCE) ||
                !subghz_check_pulse(raw_data[i+1], delay * pro->syncFactor.low, RECEIVE_TOLERANCE)) {
                continue;
            }

            uint32_t code = 0;
            int bit_count = 0;
            size_t k = i + 2;

            while (k < count - 1 && bit_count < 32) {
                bool p_high = (raw_data[k] > 0);
                bool g_high = (raw_data[k+1] > 0);

                if (pro->invertedSignal) {
                    if (p_high || !g_high) break;
                } else {
                    if (!p_high || g_high) break;
                }

                // Match Zero using unified pulse checker
                if (subghz_check_pulse(raw_data[k], delay * pro->zero.high, RECEIVE_TOLERANCE) &&
                    subghz_check_pulse(raw_data[k+1], delay * pro->zero.low, RECEIVE_TOLERANCE)) {
                    code <<= 1;
                    bit_count++;
                }
                // Match One using unified pulse checker
                else if (subghz_check_pulse(raw_data[k], delay * pro->one.high, RECEIVE_TOLERANCE) &&
                         subghz_check_pulse(raw_data[k+1], delay * pro->one.low, RECEIVE_TOLERANCE)) {
                    code = (code << 1) | 1;
                    bit_count++;
                } else {
                    break; 
                }
                k += 2;
            }

            if (bit_count >= 12) {
                static char name_buf[32];
                snprintf(name_buf, sizeof(name_buf), "RCSwitch(P%d)", p + 1);
                out_data->protocol_name = name_buf;
                out_data->bit_count = bit_count;
                out_data->raw_value = code;
                out_data->serial = code;
                out_data->btn = 0;
                return true;
            }
        }
    }

    return false;
}

static size_t protocol_rcswitch_encode(const subghz_data_t* data, int32_t* pulses, size_t max_count) {
    // Protocol name for RCSwitch is formatted as "RCSwitch(PX)" where X is p+1
    int p_idx = -1;
    if (data->protocol_name && sscanf(data->protocol_name, "RCSwitch(P%d)", &p_idx) == 1) {
        p_idx -= 1; // back to 0-based
    }

    if (p_idx < 0 || p_idx >= NUM_PROTO) return 0;
    const rcswitch_proto_t* pro = &proto[p_idx];
    
    if (max_count < (size_t)(data->bit_count * 2 + 2)) return 0;

    uint32_t delay = pro->pulseLength;
    size_t idx = 0;

    // Sync Bit
    if (pro->invertedSignal) {
        pulses[idx++] = -(int32_t)(delay * pro->syncFactor.high);
        pulses[idx++] = (int32_t)(delay * pro->syncFactor.low);
    } else {
        pulses[idx++] = (int32_t)(delay * pro->syncFactor.high);
        pulses[idx++] = -(int32_t)(delay * pro->syncFactor.low);
    }

    // Data Bits
    for (int i = data->bit_count - 1; i >= 0; i--) {
        bool bit = (data->raw_value >> i) & 1;
        if (bit) {
            // Bit One
            if (pro->invertedSignal) {
                pulses[idx++] = -(int32_t)(delay * pro->one.high);
                pulses[idx++] = (int32_t)(delay * pro->one.low);
            } else {
                pulses[idx++] = (int32_t)(delay * pro->one.high);
                pulses[idx++] = -(int32_t)(delay * pro->one.low);
            }
        } else {
            // Bit Zero
            if (pro->invertedSignal) {
                pulses[idx++] = -(int32_t)(delay * pro->zero.high);
                pulses[idx++] = (int32_t)(delay * pro->zero.low);
            } else {
                pulses[idx++] = (int32_t)(delay * pro->zero.high);
                pulses[idx++] = -(int32_t)(delay * pro->zero.low);
            }
        }
    }

    return idx;
}

subghz_protocol_t protocol_rcswitch = {
    .name = "RCSwitch",
    .decode = protocol_rcswitch_decode,
    .encode = protocol_rcswitch_encode
};
