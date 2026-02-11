#include "subghz_protocol_defs.h"
#include <string.h>
#include <stdlib.h>

// Timings para Rossi (HCS301)
// Te (Elementary Period) é tipicamente 300us - 400us
// Short = 1 * Te
// Long = 2 * Te (ocorre quando o bit anterior e atual são iguais na codificação Manchester)

#define ROSSI_SHORT_MIN 200
#define ROSSI_SHORT_MAX 600
#define ROSSI_LONG_MIN  601
#define ROSSI_LONG_MAX  1000
#define ROSSI_HEADER_MIN 3500 // Header gap > 3.5ms

static bool is_short(int32_t val) {
    if (val < 0) val = -val;
    return (val >= ROSSI_SHORT_MIN && val <= ROSSI_SHORT_MAX);
}

static bool is_long(int32_t val) {
    if (val < 0) val = -val;
    return (val >= ROSSI_LONG_MIN && val <= ROSSI_LONG_MAX);
}

static bool protocol_rossi_decode(const int32_t* raw_data, size_t count, subghz_data_t* out_data) {
    if (count < 60) return false; // HCS301 tem muitos pulsos (66 bits Manchester -> até 132 transições)

    // Procurar pelo Header (Gap Longo)
    size_t start_idx = 0;
    bool header_found = false;

    for (size_t i = 0; i < count - 10; i++) {
        // O Header do Rossi é um tempo baixo longo
        // Como invertemos o sinal, pode aparecer como positivo ou negativo dependendo do repouso
        // Mas geralmente é um GAP.
        if (abs(raw_data[i]) > ROSSI_HEADER_MIN) {
            start_idx = i + 1;
            header_found = true;
            break;
        }
    }

    if (!header_found) return false;

    // Verificar se o que vem depois parece Manchester (apenas pulsos curtos e longos)
    // Não vamos fazer o decode bit-a-bit completo agora (complexo sem state machine),
    // mas vamos validar a "assinatura" do sinal: uma sequência longa de tempos T e 2T.
    
    int valid_pulses = 0;
    int total_duration = 0;
    
    for (size_t i = start_idx; i < count; i++) {
        int32_t t = raw_data[i];
        
        if (is_short(t) || is_long(t)) {
            valid_pulses++;
            total_duration += abs(t);
        } else {
            // Se encontrou algo fora do padrão (nem curto nem longo), parou
            break;
        }
    }

    // HCS301 tem 66 bits. Em Manchester, isso gera entre 66 (todos 2T) e 132 (todos 1T) símbolos de tempo.
    // Vamos ser tolerantes. Se tivermos mais de 50 pulsos válidos consecutivos após o header, é Rossi.
    if (valid_pulses > 50) {
        out_data->protocol_name = "Rossi (HCS301)";
        out_data->bit_count = 66; // Padrão HCS301
        
        // Extração de serial simples (pseudo-serial baseado nos últimos pulsos seria possível)
        // Por enquanto, apenas identificação.
        out_data->serial = 0xDEADBEEF; // Placeholder para indicar detecção de Rolling Code
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
