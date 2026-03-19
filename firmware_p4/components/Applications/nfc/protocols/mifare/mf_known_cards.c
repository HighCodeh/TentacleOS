#include "mf_known_cards.h"
#include <string.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * Priority key pools (publicly known / documented keys from Proxmark/MFOC
 * databases — same sources as our main dictionary, just pre-filtered by
 * card type so we hit them on the first pass instead of scanning all 83).
 * ------------------------------------------------------------------------- */

/* Keys found on virtually every fresh/factory MIFARE card */
static const uint8_t s_default_keys[][6] = {
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5 },   /* MAD / transport */
    { 0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5 },
    { 0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7 },   /* NDEF */
};

/* Infineon MIFARE Classic — ships with a different default set */
static const uint8_t s_infineon_keys[][6] = {
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x88, 0x44, 0x06, 0x30, 0x13, 0x15 },   /* Infineon documented default */
    { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5 },
};

/* NXP SmartMX / P60 — ISO14443-4 + Classic combo */
static const uint8_t s_smartmx_keys[][6] = {
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5 },
    { 0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7 },
    { 0x4D, 0x3A, 0x99, 0xC3, 0x51, 0xDD },
};

/* Common transit / ticketing keys (public Proxmark/MFOC database) */
static const uint8_t s_transit_keys[][6] = {
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5 },
    { 0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7 },
    { 0x4D, 0x3A, 0x99, 0xC3, 0x51, 0xDD },
    { 0x1A, 0x98, 0x2C, 0x7E, 0x45, 0x9A },
    { 0x71, 0x4C, 0x5C, 0x88, 0x6E, 0x97 },
    { 0x58, 0x7E, 0xE5, 0xF9, 0x35, 0x0F },
    { 0xA0, 0x47, 0x8C, 0xC3, 0x90, 0x91 },
    { 0x53, 0x3C, 0xB6, 0xC7, 0x23, 0xF6 },
    { 0x8F, 0xD0, 0xA4, 0xF2, 0x56, 0xE9 },
    { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF },
};

/* Access-control / building management keys (Proxmark public DB) */
static const uint8_t s_access_keys[][6] = {
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5 },
    { 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0 },
    { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF },
    { 0x4D, 0x3A, 0x99, 0xC3, 0x51, 0xDD },
    { 0x1A, 0x98, 0x2C, 0x7E, 0x45, 0x9A },
    { 0x71, 0x4C, 0x5C, 0x88, 0x6E, 0x97 },
    { 0x8F, 0xD0, 0xA4, 0xF2, 0x56, 0xE9 },
};

/* Magic/writable clone cards almost always respond to FF...FF or 00...00 */
static const uint8_t s_clone_keys[][6] = {
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5 },
    { 0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7 },
};

#define NKEYS(arr) ((int)(sizeof(arr) / sizeof(arr[0])))

/* -------------------------------------------------------------------------
 * Known card database
 *
 * Match order matters — more specific entries (UID prefix) must come before
 * generic ones with the same SAK/ATQA.
 * ------------------------------------------------------------------------- */
static const mf_known_card_t s_db[] = {

    /* ---- UID-prefix-specific entries (most specific, checked first) ---- */

    /* NXP manufactured: UID[0] == 0x04 */
    {
        .name           = "NXP MIFARE Classic 1K",
        .hint           = "Genuine NXP (cascade byte 0x04)",
        .sak            = 0x08,
        .atqa           = { 0x00, 0x04 },
        .uid_prefix     = { 0x04 },
        .uid_prefix_len = 1,
        .hint_keys      = s_default_keys,
        .hint_key_count = NKEYS(s_default_keys),
    },
    {
        .name           = "NXP MIFARE Classic 4K",
        .hint           = "Genuine NXP (cascade byte 0x04)",
        .sak            = 0x18,
        .atqa           = { 0x00, 0x02 },
        .uid_prefix     = { 0x04 },
        .uid_prefix_len = 1,
        .hint_keys      = s_default_keys,
        .hint_key_count = NKEYS(s_default_keys),
    },

    /* ---- Clone / magic cards ---- */

    /* SAK=0x08, ATQA=00 44 — Gen2 / CUID / direct-write clones */
    {
        .name           = "MIFARE Classic 1K Clone (Gen2/CUID)",
        .hint           = "UID changeable, writable block 0",
        .sak            = 0x08,
        .atqa           = { 0x00, 0x44 },
        .uid_prefix_len = 0,
        .hint_keys      = s_clone_keys,
        .hint_key_count = NKEYS(s_clone_keys),
    },
    /* SAK=0x18, ATQA=00 42 — 4K clone */
    {
        .name           = "MIFARE Classic 4K Clone",
        .hint           = "UID changeable clone",
        .sak            = 0x18,
        .atqa           = { 0x00, 0x42 },
        .uid_prefix_len = 0,
        .hint_keys      = s_clone_keys,
        .hint_key_count = NKEYS(s_clone_keys),
    },
    /* SAK=0x88 — Infineon SLE66R35 */
    {
        .name           = "Infineon MIFARE Classic 1K",
        .hint           = "SLE66R35, used in older access/transit systems",
        .sak            = 0x88,
        .atqa           = { 0x00, 0x04 },
        .uid_prefix_len = 0,
        .hint_keys      = s_infineon_keys,
        .hint_key_count = NKEYS(s_infineon_keys),
    },

    /* ---- ISO14443-4 combo chips ---- */

    /* SAK=0x28 — NXP SmartMX / P60 MIFARE Classic 1K + ISO14443-4 */
    {
        .name           = "NXP SmartMX MIFARE Classic 1K",
        .hint           = "P60/P40 combo chip, SAK=0x28",
        .sak            = 0x28,
        .atqa           = { 0x00, 0x04 },
        .uid_prefix_len = 0,
        .hint_keys      = s_smartmx_keys,
        .hint_key_count = NKEYS(s_smartmx_keys),
    },
    /* SAK=0x38 — NXP SmartMX 4K */
    {
        .name           = "NXP SmartMX MIFARE Classic 4K",
        .hint           = "P60/P40 combo chip, SAK=0x38",
        .sak            = 0x38,
        .atqa           = { 0x00, 0x02 },
        .uid_prefix_len = 0,
        .hint_keys      = s_smartmx_keys,
        .hint_key_count = NKEYS(s_smartmx_keys),
    },

    /* ---- Transit / ticketing (no specific UID prefix, known by ATQA) ---- */

    /* Many Brazilian transit cards: SAK=0x08, ATQA=00 04 — generic 1K */
    {
        .name           = "Transit/Ticketing Card (1K)",
        .hint           = "Common for metro/bus access systems",
        .sak            = 0x08,
        .atqa           = { 0x00, 0x04 },
        .uid_prefix_len = 0,
        .hint_keys      = s_transit_keys,
        .hint_key_count = NKEYS(s_transit_keys),
    },
    /* 4K transit cards */
    {
        .name           = "Transit/Ticketing Card (4K)",
        .hint           = "Large-memory transit card",
        .sak            = 0x18,
        .atqa           = { 0x00, 0x02 },
        .uid_prefix_len = 0,
        .hint_keys      = s_transit_keys,
        .hint_key_count = NKEYS(s_transit_keys),
    },

    /* ---- Access control ---- */

    /* SAK=0x08, ATQA=00 04 with common access-control key patterns */
    {
        .name           = "Access Control Card (1K)",
        .hint           = "Building/door access system",
        .sak            = 0x08,
        .atqa           = { 0x08, 0x00 },
        .uid_prefix_len = 0,
        .hint_keys      = s_access_keys,
        .hint_key_count = NKEYS(s_access_keys),
    },

    /* ---- MIFARE Mini ---- */
    {
        .name           = "MIFARE Mini",
        .hint           = "320 bytes / 5 sectors",
        .sak            = 0x09,
        .atqa           = { 0x00, 0x04 },
        .uid_prefix_len = 0,
        .hint_keys      = s_default_keys,
        .hint_key_count = NKEYS(s_default_keys),
    },
};

#define DB_SIZE ((int)(sizeof(s_db) / sizeof(s_db[0])))

/* -------------------------------------------------------------------------
 * Matching logic
 * ------------------------------------------------------------------------- */
const mf_known_card_t* mf_known_cards_match(uint8_t        sak,
                                              const uint8_t  atqa[2],
                                              const uint8_t* uid,
                                              uint8_t        uid_len)
{
    const mf_known_card_t* generic_match = NULL;

    for (int i = 0; i < DB_SIZE; i++) {
        const mf_known_card_t* e = &s_db[i];

        if (e->sak != sak) continue;
        if (e->atqa[0] != atqa[0] || e->atqa[1] != atqa[1]) continue;

        if (e->uid_prefix_len > 0) {
            /* UID-specific entry */
            if (uid_len < e->uid_prefix_len) continue;
            if (memcmp(uid, e->uid_prefix, e->uid_prefix_len) != 0) continue;
            /* Specific match wins immediately */
            return e;
        } else {
            /* Generic match — remember first, keep looking for specific */
            if (generic_match == NULL) generic_match = e;
        }
    }

    return generic_match;
}
