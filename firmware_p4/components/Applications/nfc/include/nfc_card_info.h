/**
 * @file nfc_card_info.h
 * @brief Card identification helpers (manufacturer + type).
 */
#ifndef NFC_CARD_INFO_H
#define NFC_CARD_INFO_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char* name;
    const char* full_name;
    bool        is_mf_classic;
    bool        is_mf_ultralight;
    bool        is_iso_dep;
} card_type_info_t;

const char* get_manufacturer_name(uint8_t uid0);
card_type_info_t identify_card(uint8_t sak, const uint8_t atqa[2]);

#endif
