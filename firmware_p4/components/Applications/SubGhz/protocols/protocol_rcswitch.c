#include "subghz_protocol_defs.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Definição dos protocolos RCSwitch
typedef struct {
    uint16_t pulse_len;
    struct { uint8_t high; uint8_t low; } sync;
    struct { uint8_t high; uint8_t low; } zero;
    struct { uint8_t high; uint8_t low; } one;
    bool inverted;
} rcswitch_proto_t;

static const rcswitch_proto_t rc_protos[] = {
  { 350, {  1, 31 }, {  1,  3 }, {  3,  1 }, false },    // protocol 1 (Princeton/PT2262)
  { 650, {  1, 10 }, {  1,  2 }, {  2,  1 }, false },    // protocol 2
  { 100, { 30, 71 }, {  4, 11 }, {  9,  6 }, false },    // protocol 3
  { 380, {  1,  6 }, {  1,  3 }, {  3,  1 }, false },    // protocol 4
  { 500, {  6, 14 }, {  1,  2 }, {  2,  1 }, false },    // protocol 5
  { 450, { 23,  1 }, {  1,  2 }, {  2,  1 }, true },     // protocol 6 (HT6P20B)
  { 150, {  2, 62 }, {  1,  6 }, {  6,  1 }, false },    // protocol 7
  { 200, {  3, 130}, {  7, 16 }, {  3,  16}, false},     // protocol 8
  { 200, { 130, 7 }, {  16, 7 }, { 16,  3 }, true},      // protocol 9
  { 365, { 18,  1 }, {  3,  1 }, {  1,  3 }, true },     // protocol 10
  { 270, { 36,  1 }, {  1,  2 }, {  2,  1 }, true },     // protocol 11
  { 320, { 36,  1 }, {  1,  2 }, {  2,  1 }, true }      // protocol 12
};

#define RC_PROTO_COUNT (sizeof(rc_protos)/sizeof(rcswitch_proto_t))
#define TOLERANCE 60 // % tolerance (RCSwitch uses 60)

static bool check_pulse(int32_t raw_len, uint16_t base_len, uint8_t multiplier) {
    if (raw_len < 0) raw_len = -raw_len;
    int32_t target = base_len * multiplier;
    int32_t tol = target * TOLERANCE / 100;
    return (raw_len >= target - tol) && (raw_len <= target + tol);
}

static bool check_polarity(int32_t raw_len, bool expect_high) {
    if (expect_high) return raw_len > 0;
    else return raw_len < 0;
}

static bool protocol_rcswitch_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 20) return false;

    // Tenta cada protocolo
    for (int p = 0; p < RC_PROTO_COUNT; p++) {
        const rcswitch_proto_t* proto = &rc_protos[p];
        
        // Determina a polaridade esperada para o primeiro pulso do par (Pulse)
        // Normal (inverted=false): Pulse=High (>0), Gap=Low (<0)
        // Inverted (inverted=true): Pulse=Low (<0), Gap=High (>0)
        bool expect_pulse_high = !proto->inverted;

        // RCSwitch geralmente procura Sync Bit primeiro.
        // Sync pode estar no começo ou no fim.
        // Vamos varrer o sinal procurando Sync.
        
        for (size_t i = 0; i < count - 1; i++) {
            // Check Sync
            int32_t p_sync = raw_data[i];
            int32_t g_sync = raw_data[i+1];
            
            // 1. Verifica Polaridade do Sync
            if (!check_polarity(p_sync, expect_pulse_high) || 
                !check_polarity(g_sync, !expect_pulse_high)) {
                continue;
            }

            // 2. Verifica Duração do Sync
            if (check_pulse(p_sync, proto->pulse_len, proto->sync.high) &&
                check_pulse(g_sync, proto->pulse_len, proto->sync.low)) {
                
                // Sync Found! Agora decodificar os bits seguintes
                uint32_t decoded_val = 0;
                int bits = 0;
                size_t k = i + 2;
                bool fail = false;
                
                // Tenta ler até 32 bits ou fim do buffer
                while (k < count - 1 && bits < 32) {
                    int32_t p0 = raw_data[k];
                    int32_t g0 = raw_data[k+1];

                    // Verifica Polaridade dos Dados
                    if (!check_polarity(p0, expect_pulse_high) || 
                        !check_polarity(g0, !expect_pulse_high)) {
                        fail = true;
                        break;
                    }
                    
                    // Test 0
                    if (check_pulse(p0, proto->pulse_len, proto->zero.high) && 
                        check_pulse(g0, proto->pulse_len, proto->zero.low)) {
                        decoded_val = (decoded_val << 1) | 0;
                        bits++;
                    } 
                    // Test 1
                    else if (check_pulse(p0, proto->pulse_len, proto->one.high) && 
                             check_pulse(g0, proto->pulse_len, proto->one.low)) {
                        decoded_val = (decoded_val << 1) | 1;
                        bits++;
                    } else {
                        fail = true;
                        break; // Not a bit
                    }
                    k += 2;
                }
                
                if (!fail && bits >= 8) {
                    // Sucesso!
                    static char proto_name[24];
                    snprintf(proto_name, sizeof(proto_name), "RCSwitch(%d)", p + 1);
                    out_data->protocol_name = proto_name;
                    
                    out_data->bit_count = bits;
                    out_data->raw_value = decoded_val;
                    out_data->serial = decoded_val; // Depende do protocolo
                    out_data->btn = 0;
                    
                    return true;
                }
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
