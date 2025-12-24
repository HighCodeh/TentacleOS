#include "subghz_protocol_registry.h"
#include <stddef.h>

// Declaração externa dos protocolos implementados
extern subghz_protocol_t protocol_princeton;
extern subghz_protocol_t protocol_came;
extern subghz_protocol_t protocol_nice_flo;
extern subghz_protocol_t protocol_ansonic;
extern subghz_protocol_t protocol_chamberlain;
extern subghz_protocol_t protocol_holtek;
extern subghz_protocol_t protocol_linear;
extern subghz_protocol_t protocol_liftmaster;
extern subghz_protocol_t protocol_rossi;
extern subghz_protocol_t protocol_rcswitch;

// Lista de protocolos ativos
static subghz_protocol_t* registry[] = {
    &protocol_rossi, // Check first (distinctive header)
    &protocol_princeton,
    &protocol_came,
    &protocol_nice_flo,
    &protocol_ansonic,
    &protocol_chamberlain,
    &protocol_holtek,
    &protocol_linear,
    &protocol_liftmaster,
    &protocol_rcswitch, // Generic fallback
    NULL
};

bool subghz_process_raw(const int32_t* raw_data, size_t count, subghz_data_t* out_result) {
    for (int i = 0; registry[i] != NULL; i++) {
        if (registry[i]->decode(raw_data, count, out_result)) {
            return true;
        }
    }
    return false;
}
