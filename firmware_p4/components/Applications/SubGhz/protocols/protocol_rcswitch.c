#include "subghz_protocol_defs.h"
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

static inline uint32_t diff(uint32_t a, uint32_t b) {
    return (a > b) ? (a - b) : (b - a);
}

static bool protocol_rcswitch_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 10) return false;

    // RCSwitch logic: Search for a sync gap, calculate base delay, then decode.
    for (int p = 0; p < NUM_PROTO; p++) {
        const rcswitch_proto_t* pro = &proto[p];
        
        for (size_t i = 0; i < count - 1; i++) {
            // Check Sync Polarity
            bool first_is_high = (raw_data[i] > 0);
            bool second_is_high = (raw_data[i+1] > 0);
            
            if (pro->invertedSignal) {
                if (first_is_high || !second_is_high) continue; 
            } else {
                if (!first_is_high || second_is_high) continue;
            }

            uint32_t dur1 = abs(raw_data[i]);
            uint32_t dur2 = abs(raw_data[i+1]);

            // Calculate base delay from the longer part of the sync factor
            uint32_t sync_long_factor = (pro->syncFactor.low > pro->syncFactor.high) ? pro->syncFactor.low : pro->syncFactor.high;
            uint32_t dur_long = (pro->syncFactor.low > pro->syncFactor.high) ? dur2 : dur1;
            
            if (sync_long_factor == 0) continue;
            uint32_t delay = dur_long / sync_long_factor;
            if (delay < 50 || delay > 1200) continue; 

            uint32_t tolerance = delay * RECEIVE_TOLERANCE / 100;

            // Validate entire Sync Bit
            if (diff(dur1, delay * pro->syncFactor.high) > tolerance ||
                diff(dur2, delay * pro->syncFactor.low) > tolerance) {
                continue;
            }

            // Sync Found! Decode bits using derived 'delay'
            uint32_t code = 0;
            int bit_count = 0;
            size_t k = i + 2;

            while (k < count - 1 && bit_count < 32) {
                uint32_t d_p = abs(raw_data[k]);
                uint32_t d_g = abs(raw_data[k+1]);
                bool p_high = (raw_data[k] > 0);
                bool g_high = (raw_data[k+1] > 0);

                // Polarity Check for Bits
                if (pro->invertedSignal) {
                    if (p_high || !g_high) break;
                } else {
                    if (!p_high || g_high) break;
                }

                // Match Zero
                if (diff(d_p, delay * pro->zero.high) < tolerance &&
                    diff(d_g, delay * pro->zero.low) < tolerance) {
                    code <<= 1;
                    bit_count++;
                }
                // Match One
                else if (diff(d_p, delay * pro->one.high) < tolerance &&
                         diff(d_g, delay * pro->one.low) < tolerance) {
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

subghz_protocol_t protocol_rcswitch = {
    .name = "RCSwitch",
    .decode = protocol_rcswitch_decode,
    .encode = NULL
};
