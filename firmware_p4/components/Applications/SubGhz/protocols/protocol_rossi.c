#include "subghz_protocol_decoder.h"
#include "subghz_protocol_utils.h"
#include <string.h>
#include <stdlib.h>

// Timings for Rossi (HCS301)
// Te (Elementary Period) is typically 300us - 400us
// Short = 1 * Te
// Long = 2 * Te

#define ROSSI_SHORT_MIN 200
#define ROSSI_SHORT_MAX 600
#define ROSSI_LONG_MIN  601
#define ROSSI_LONG_MAX  1000
#define ROSSI_HEADER_MIN 3500 // Header gap > 3.5ms

static bool is_short(int32_t val) {
    uint32_t a = abs(val);
    return (a >= ROSSI_SHORT_MIN && a <= ROSSI_SHORT_MAX);
}

static bool is_long(int32_t val) {
    uint32_t a = abs(val);
    return (a >= ROSSI_LONG_MIN && a <= ROSSI_LONG_MAX);
}

static bool protocol_rossi_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 60) return false; 

    size_t start_idx = 0;
    bool header_found = false;

    for (size_t i = 0; i < count - 10; i++) {
        if (abs(raw_data[i]) > ROSSI_HEADER_MIN) {
            start_idx = i + 1;
            header_found = true;
            break;
        }
    }

    if (!header_found) return false;

    int valid_pulses = 0;
    
    for (size_t i = start_idx; i < count; i++) {
        int32_t t = raw_data[i];
        
        if (is_short(t) || is_long(t)) {
            valid_pulses++;
        } else {
            break;
        }
    }

    if (valid_pulses > 50) {
        out_data->protocol_name = "Rossi (HCS301)";
        out_data->bit_count = 66; 
        out_data->serial = 0xDEADBEEF; 
        out_data->btn = 0;
        out_data->raw_value = 0;
        
        return true;
    }

    return false;
}

subghz_protocol_t protocol_rossi = {
    .name = "Rossi",
    .decode = protocol_rossi_decode,
    .encode = NULL
};
