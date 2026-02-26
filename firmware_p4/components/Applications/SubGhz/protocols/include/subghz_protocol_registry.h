#ifndef SUBGHZ_PROTOCOL_REGISTRY_H
#define SUBGHZ_PROTOCOL_REGISTRY_H

#include "subghz_protocol_decoder.h"

/**
 * @brief Initialize the protocol registry
 */
void subghz_protocol_registry_init(void);

/**
 * @brief Run all registered decoders on the signal
 * @return true if a protocol claimed the signal
 */
bool subghz_protocol_registry_decode_all(const int32_t* pulses, size_t count, subghz_data_t* out_data);

/**
 * @brief Find a protocol by name
 * @return pointer to protocol structure or NULL if not found
 */
const subghz_protocol_t* subghz_protocol_registry_get_by_name(const char* name);

#endif // SUBGHZ_PROTOCOL_REGISTRY_H
