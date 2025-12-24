#include "subghz_protocol_defs.h"
#include <string.h>

#define CAME_SHORT 320
#define CAME_LONG  640
#define CAME_TOL   150 // Tolerância

static bool is_within(int32_t val, int32_t target) {
    if (val < 0) val = -val; // Aceita magnitude
    return (val >= target - CAME_TOL) && (val <= target + CAME_TOL);
}

static bool protocol_came_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 24) return false; // CAME 12 bits precisa de 24 transições (12 pares High/Low)

    uint32_t decoded_data = 0;
    int bits_found = 0;

    // CAME geralmente começa com um Pilot bit (gap longo) e depois Start bit.
    // Vamos varrer procurando uma sequência válida de bits.
    
    for (size_t i = 0; i < count - 1; i += 2) {
        int32_t pulse = raw_data[i];     // Deveria ser High
        int32_t gap   = raw_data[i+1];   // Deveria ser Low

        // Verifica polaridade (opcional se o sinal estiver muito ruidoso, mas idealmente P=+, G=-)
        if (pulse < 0) { 
            // Se começou com negativo, talvez estejamos desalinhados. Tenta pular 1.
            if (i == 0) { i--; continue; } 
            return false; 
        }

        int bit = -1;

        // Bit 0: Short Pulse, Long Gap
        if (is_within(pulse, CAME_SHORT) && is_within(gap, CAME_LONG)) {
            bit = 0;
        }
        // Bit 1: Long Pulse, Short Gap
        else if (is_within(pulse, CAME_LONG) && is_within(gap, CAME_SHORT)) {
            bit = 1;
        }
        // Start/Pilot detection could be here
        else {
            // Se falhou, reseta
            decoded_data = 0;
            bits_found = 0;
            continue;
        }

        if (bit != -1) {
            decoded_data = (decoded_data << 1) | bit;
            bits_found++;
            
            // CAME-12 (12 bits) ou CAME-24 (24 bits)
            if (bits_found == 12) {
                out_data->protocol_name = "CAME 12bit";
                out_data->bit_count = 12;
                out_data->raw_value = decoded_data;
                out_data->serial = decoded_data; 
                out_data->btn = 0; // CAME não separa botão fixo, varia
                return true;
            }
             if (bits_found == 24) {
                out_data->protocol_name = "CAME 24bit";
                out_data->bit_count = 24;
                out_data->raw_value = decoded_data;
                out_data->serial = decoded_data;
                return true;
            }
        }
    }

    return false;
}

static void protocol_came_encode(const subghz_data_t* in_data, int32_t** out_raw, size_t* out_count) {
    // Implementar Encoder Futuramente
}

subghz_protocol_t protocol_came = {
    .name = "CAME",
    .decode = protocol_came_decode,
    .encode = protocol_came_encode
};
