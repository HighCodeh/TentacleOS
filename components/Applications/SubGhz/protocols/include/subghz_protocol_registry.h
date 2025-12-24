#ifndef SUBGHZ_PROTOCOL_REGISTRY_H
#define SUBGHZ_PROTOCOL_REGISTRY_H

#include "subghz_protocol_defs.h"

// Tenta decodificar usando todos os protocolos conhecidos
bool subghz_process_raw(const int32_t* raw_data, size_t count, subghz_data_t* out_result);

#endif
