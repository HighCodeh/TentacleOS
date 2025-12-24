#include "subghz_protocol_defs.h"
#include <string.h>

#define ANSONIC_SHORT 555
#define ANSONIC_LONG  1111
#define ANSONIC_TOL   200

static bool is_within(int32_t val, int32_t target) {
    if (val < 0) val = -val;
    return (val >= target - ANSONIC_TOL) && (val <= target + ANSONIC_TOL);
}

static bool protocol_ansonic_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 20) return false;

    uint32_t decoded_data = 0;
    int bits_found = 0;

    // Bruce: '0' = {-1111, 555} (Gap, Pulse) ou (Pulse, Gap)?
    // Se o código C++ diz {-GAP, PULSE}, e nosso receiver é PULSE, GAP...
    // Vamos assumir que o Bruce define Gap, Pulse.
    // Mas protocolos PWM geralmente são Pulse, Gap.
    // Vamos tentar a lógica PWM padrão: Short/Long.
    
    for (size_t i = 0; i < count - 1; i += 2) {
        int32_t pulse = raw_data[i];
        int32_t gap   = raw_data[i+1];

        if (pulse < 0) { if (i==0){i--; continue;} return false; }

        int bit = -1;

        // Bit 0: Pulse 555, Gap 1111 (Short, Long)
        if (is_within(pulse, ANSONIC_SHORT) && is_within(gap, ANSONIC_LONG)) {
            bit = 0;
        }
        // Bit 1: Pulse 1111, Gap 555 (Long, Short)
        else if (is_within(pulse, ANSONIC_LONG) && is_within(gap, ANSONIC_SHORT)) {
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
            
            // Ansonic costuma ter 12 bits ou mais? Vamos aceitar >10
            if (bits_found >= 10 && bits_found <= 16) { 
                 // Guarda resultado parcial, se o proximo falhar, validamos esse
                 // Simplificação: valida com 12 bits
                 if (bits_found == 12) {
                    out_data->protocol_name = "Ansonic";
                    out_data->bit_count = bits_found;
                    out_data->raw_value = decoded_data;
                    out_data->serial = decoded_data;
                    return true;
                 }
            }
        }
    }
    return false;
}

subghz_protocol_t protocol_ansonic = {
    .name = "Ansonic",
    .decode = protocol_ansonic_decode,
    .encode = NULL
};
