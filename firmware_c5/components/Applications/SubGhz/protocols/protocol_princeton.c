#include "subghz_protocol_defs.h"
#include <string.h>
#include <stdlib.h>

#define PRINCETON_SHORT 350
#define PRINCETON_LONG  1050
#define PRINCETON_SYNC  10000
#define TOLERANCE       250 

static bool is_within(int32_t val, int32_t target) {
    if (val < 0) val = -val;
    return (val >= target - TOLERANCE) && (val <= target + TOLERANCE);
}

static bool protocol_princeton_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 24) return false;

    uint32_t decoded_data = 0;
    int bits_found = 0;

    // Princeton envia cada bit lógico como DOIS ciclos de pulso/pausa.
    // Bit 0: (S, L), (S, L)
    // Bit 1: (L, S), (L, S)
    // Bit F: (S, L), (L, S)
    // S=Short(~350), L=Long(~1050)
    
    // Avança de 4 em 4 (2 ciclos = 4 transições: P G P G)
    for (size_t i = 0; i < count - 3; i += 4) {
        int32_t p1 = raw_data[i];
        int32_t g1 = raw_data[i+1];
        int32_t p2 = raw_data[i+2];
        int32_t g2 = raw_data[i+3];

        // Validar magnitudes
        if (p1 < 0) { if (i==0){i--; continue;} return false; } // Sincronia
        if (g1 > 0) g1 = -g1;
        if (g2 > 0) g2 = -g2;
        g1 = abs(g1);
        g2 = abs(g2);

        int bit = -1;

        bool p1_s = is_within(p1, PRINCETON_SHORT);
        bool p1_l = is_within(p1, PRINCETON_LONG);
        bool g1_s = is_within(g1, PRINCETON_SHORT);
        bool g1_l = is_within(g1, PRINCETON_LONG);
        
        bool p2_s = is_within(p2, PRINCETON_SHORT);
        bool p2_l = is_within(p2, PRINCETON_LONG);
        bool g2_s = is_within(g2, PRINCETON_SHORT);
        bool g2_l = is_within(g2, PRINCETON_LONG);

        // Bit 0: SL SL
        if (p1_s && g1_l && p2_s && g2_l) {
            bit = 0;
        }
        // Bit 1: LS LS
        else if (p1_l && g1_s && p2_l && g2_s) {
            bit = 1;
        }
        // Bit F: SL LS
        else if (p1_s && g1_l && p2_l && g2_s) {
            bit = 0; // Tratando Floating como 0 para simplificar, ou lógica separada
        } 
        else {
            // Se falhar a correspondencia estrita, não é Princeton
            decoded_data = 0;
            bits_found = 0;
            
            // Se achar sync, pode reiniciar
            if (g2 > PRINCETON_SYNC) {
                // i vai incrementar +4, proximo loop começa novo
                continue;
            }
            continue;
        }

        if (bit != -1) {
            decoded_data = (decoded_data << 1) | bit;
            bits_found++;
            
            if (bits_found >= 12) {
                // Check if result is valid (not 0x000)
                if (decoded_data == 0) {
                    // Princeton valido dificilmente é tudo 0. 
                    // Isso ajuda a filtrar Rossi (que pode gerar pulsos curtos parecidos)
                    // Mas se o controle for realmente 0x0... (Raro)
                    // Vamos continuar buscando.
                } else {
                    out_data->protocol_name = "Princeton";
                    out_data->bit_count = bits_found;
                    out_data->raw_value = decoded_data;
                    out_data->serial = decoded_data >> 4;
                    out_data->btn = decoded_data & 0x0F;
                    return true;
                }
            }
        }
    }

    return false;
}

static void protocol_princeton_encode(const subghz_data_t* in_data, int32_t** out_raw, size_t* out_count) {
}

subghz_protocol_t protocol_princeton = {
    .name = "Princeton",
    .decode = protocol_princeton_decode,
    .encode = protocol_princeton_encode
};