#include "nfc_reader.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "nfc_common.h"
#include "poller.h"
#include "iso_dep.h"
#include "t4t.h"
#include "mf_classic.h"
#include "mf_classic_emu.h"
#include "mf_classic_writer.h"
#include "mf_plus.h"
#include "mf_ultralight.h"
#include "nfc_card_info.h"
#include "mf_key_dict.h"
#include "mf_key_cache.h"
#include "mf_nested.h"
#include "mf_known_cards.h"
#include "nfc_store.h"

static const char* TAG = "hb_main";

mfc_emu_card_data_t s_emu_card = { 0 };
bool s_emu_data_ready = false;


static void get_access_bits_for_block(const uint8_t trailer[16], int blk,
                                       uint8_t* c1, uint8_t* c2, uint8_t* c3)
{
    uint8_t b7 = trailer[7], b8 = trailer[8];
    *c1 = (b7 >> (4 + blk)) & 1U;
    *c2 = (b8 >> blk) & 1U;
    *c3 = (b8 >> (4 + blk)) & 1U;
}

static const char* access_cond_data_str(uint8_t c1, uint8_t c2, uint8_t c3)
{
    uint8_t bits = (c1 << 2) | (c2 << 1) | c3;
    switch (bits) {
    case 0: return "rd:AB  wr:AB  inc:AB  dec:AB";
    case 1: return "rd:AB  wr:--  inc:--  dec:AB";
    case 2: return "rd:AB  wr:--  inc:--  dec:--";
    case 3: return "rd:B   wr:B   inc:--  dec:--";
    case 4: return "rd:AB  wr:B   inc:--  dec:--";
    case 5: return "rd:B   wr:--  inc:--  dec:--";
    case 6: return "rd:AB  wr:B   inc:B   dec:AB";
    case 7: return "rd:--  wr:--  inc:--  dec:--";
    default: return "FAIL";
    }
}

static const char* access_cond_trailer_str(uint8_t c1, uint8_t c2, uint8_t c3)
{
    uint8_t bits = (c1 << 2) | (c2 << 1) | c3;
    switch (bits) {
    case 0: return "KeyA:wr_A  AC:rd_A    KeyB:rd_A/wr_A";
    case 1: return "KeyA:wr_A  AC:rd_A/wr_A KeyB:rd_A/wr_A";
    case 2: return "KeyA:--    AC:rd_A     KeyB:rd_A";
    case 3: return "KeyA:wr_B  AC:rd_AB/wr_B KeyB:wr_B";
    case 4: return "KeyA:wr_B  AC:rd_AB    KeyB:wr_B";
    case 5: return "KeyA:--    AC:rd_AB/wr_B KeyB:--";
    case 6: return "KeyA:--    AC:rd_AB    KeyB:--";
    case 7: return "KeyA:--    AC:rd_AB    KeyB:--";
    default: return "FAIL";
    }
}

/** Check if Key B is readable from trailer given access bits for block 3 */
static bool is_key_b_readable(uint8_t c1, uint8_t c2, uint8_t c3)
{
    uint8_t bits = (c1 << 2) | (c2 << 1) | c3;
    return (bits == 0 || bits == 1 || bits == 2);
}

static bool verify_access_bits_parity(const uint8_t trailer[16])
{
    uint8_t b6 = trailer[6], b7 = trailer[7], b8 = trailer[8];
    for (int blk = 0; blk < 4; blk++) {
        uint8_t c1     = (b7 >> (4 + blk)) & 1U;
        uint8_t c1_inv = (~b6 >> blk) & 1U;
        uint8_t c2     = (b8 >> blk) & 1U;
        uint8_t c2_inv = (~b6 >> (4 + blk)) & 1U;
        uint8_t c3     = (b8 >> (4 + blk)) & 1U;
        uint8_t c3_inv = (~b7 >> blk) & 1U;
        if (c1 != c1_inv || c2 != c2_inv || c3 != c3_inv) return false;
    }
    return true;
}

static bool is_value_block(const uint8_t data[16])
{
    if (data[0]  != data[8]  || data[1]  != data[9]  ||
        data[2]  != data[10] || data[3]  != data[11]) return false;
    if ((uint8_t)(data[0] ^ 0xFF) != data[4]  ||
        (uint8_t)(data[1] ^ 0xFF) != data[5]  ||
        (uint8_t)(data[2] ^ 0xFF) != data[6]  ||
        (uint8_t)(data[3] ^ 0xFF) != data[7])  return false;
    if (data[12] != data[14]) return false;
    if ((uint8_t)(data[12] ^ 0xFF) != data[13]) return false;
    if ((uint8_t)(data[12] ^ 0xFF) != data[15]) return false;
    return true;
}

static int32_t decode_value_block(const uint8_t data[16])
{
    return (int32_t)((uint32_t)data[0] | ((uint32_t)data[1] << 8) |
                     ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24));
}

static bool is_block_empty(const uint8_t data[16])
{
    for (int i = 0; i < 16; i++) {
        if (data[i] != 0x00) return false;
    }
    return true;
}

static bool has_ascii_content(const uint8_t data[16])
{
    int printable = 0;
    for (int i = 0; i < 16; i++) {
        if (data[i] >= 0x20 && data[i] <= 0x7E) printable++;
    }
    return printable >= 8;
}

static void hex_str(const uint8_t* data, size_t len, char* buf, size_t buf_sz)
{
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 3 < buf_sz; i++) {
        pos += (size_t)snprintf(buf + pos, buf_sz - pos,
                                "%02X%s", data[i], i + 1 < len ? " " : "");
    }
}

static void hex_str_key(const uint8_t* data, char* buf, size_t buf_sz)
{
    snprintf(buf, buf_sz, "%02X %02X %02X %02X %02X %02X",
             data[0], data[1], data[2], data[3], data[4], data[5]);
}

static void ascii_str(const uint8_t* data, size_t len, char* buf, size_t buf_sz)
{
    size_t i;
    for (i = 0; i < len && i + 1 < buf_sz; i++) {
        buf[i] = (data[i] >= 0x20 && data[i] <= 0x7E) ? (char)data[i] : '.';
    }
    buf[i] = '\0';
}

static void analyze_manufacturer_block(const uint8_t blk0[16], const nfc_iso14443a_data_t* card)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Manufacturer Block Analysis");

    if (card->uid_len == 4) {
        uint8_t bcc = blk0[0] ^ blk0[1] ^ blk0[2] ^ blk0[3];
        bool bcc_ok = (bcc == blk0[4]);
        ESP_LOGI(TAG, "UID: %02X %02X %02X %02X BCC: %02X %s",
                 blk0[0], blk0[1], blk0[2], blk0[3], blk0[4],
                 bcc_ok ? "OK" : "MISMATCH!");
        if (!bcc_ok) {
            ESP_LOGW(TAG, "BCC should be %02X; card may be clone/magic!", bcc);
        }
    }

    ESP_LOGI(TAG, "SAK stored: 0x%02X ATQA stored: %02X %02X",
             blk0[5], blk0[6], blk0[7]);

    const char* mfr = get_manufacturer_name(blk0[0]);
    if (mfr) {
        ESP_LOGI(TAG, "Manufacturer: %s (ID 0x%02X)", mfr, blk0[0]);
    } else {
        ESP_LOGI(TAG, "Manufacturer: Unknown (ID 0x%02X)", blk0[0]);
    }

    if (blk0[0] == 0xE0 || blk0[0] == 0x00 || blk0[0] == 0xFF) {
        ESP_LOGW(TAG, "UID byte 0 (0x%02X) is not a registered NXP manufacturer", blk0[0]);
        ESP_LOGW(TAG, "Possible card clone (Gen2/CUID or editable UID)");
    }

    char mfr_data[40];
    hex_str(&blk0[8], 8, mfr_data, sizeof(mfr_data));
    ESP_LOGI(TAG, "MFR data: %s", mfr_data);

    bool nxp_pattern = (blk0[0] == 0x04);
    if (nxp_pattern) {
        ESP_LOGI(TAG, "NXP manufacturer confirmed; card likely original");
    }

    ESP_LOGI(TAG, "");
}

static void check_mad(const uint8_t blk1[16], const uint8_t blk2[16], bool key_was_mad)
{
    uint8_t mad_version = blk1[1] >> 6;
    bool has_mad = (blk1[0] != 0x00 || blk1[1] != 0x00);

    if (key_was_mad || has_mad) {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "MAD (MIFARE Application Directory)");

        if (key_was_mad) {
            ESP_LOGI(TAG, "Key A = MAD Key (A0 A1 A2 A3 A4 A5)");
        }

        ESP_LOGI(TAG, "MAD CRC: 0x%02X", blk1[0]);
        ESP_LOGI(TAG, "MAD Info: 0x%02X (version %d)", blk1[1], mad_version);

        ESP_LOGI(TAG, "Sector AIDs:");
        for (int s = 1; s <= 7; s++) {
            uint16_t aid = (uint16_t)blk1[s * 2] | ((uint16_t)blk1[s * 2 + 1] << 8);
            if (aid != 0x0000) {
                const char* aid_name = "";
                if (aid == 0x0003) aid_name = " (NDEF)";
                else if (aid == 0x0001) aid_name = " (Defect)";
                else if (aid == 0x0004) aid_name = " (Card Holder)";
                ESP_LOGI(TAG, "Sector %02d AID 0x%04X%s", s, aid, aid_name);
            }
        }
        for (int s = 8; s <= 15; s++) {
            int idx = (s - 8) * 2;
            uint16_t aid = (uint16_t)blk2[idx] | ((uint16_t)blk2[idx + 1] << 8);
            if (aid != 0x0000) {
                const char* aid_name = "";
                if (aid == 0x0003) aid_name = " (NDEF)";
                ESP_LOGI(TAG, "Sector %02d AID 0x%04X%s", s, aid, aid_name);
            }
        }

        bool all_zero = true;
        for (int i = 2; i < 16; i++) { if (blk1[i]) all_zero = false; }
        for (int i = 0; i < 16; i++) { if (blk2[i]) all_zero = false; }
        if (all_zero) {
            ESP_LOGI(TAG, "(no registered applications)");
        }

        ESP_LOGI(TAG, "");
    }
}

typedef struct {
    uint32_t nonces[40];
    int      count;
    bool     all_same;
    bool     weak_prng;
} prng_analysis_t;

static prng_analysis_t s_prng = { 0 };

static void prng_record_nonce(int sector, uint32_t nt)
{
    if (sector < 40) {
        s_prng.nonces[sector] = nt;
        s_prng.count++;
    }
}

static void prng_analyze(void)
{
    if (s_prng.count < 2) return;

    s_prng.all_same = true;
    int distinct = 1;
    for (int i = 1; i < 40 && s_prng.nonces[i] != 0; i++) {
        if (s_prng.nonces[i] != s_prng.nonces[0]) {
            s_prng.all_same = false;
        }

        bool found = false;
        for (int j = 0; j < i; j++) {
            if (s_prng.nonces[j] == s_prng.nonces[i]) { found = true; break; }
        }
        if (!found) distinct++;
    }

    if (s_prng.count > 4 && distinct <= 2) {
        s_prng.weak_prng = true;
    }
}

typedef struct {
    bool     key_a_found;
    bool     key_b_found;
    uint8_t  key_a[6];
    uint8_t  key_b[6];
    bool     blocks_read[16];
    uint8_t  block_data[16][16];
    int      blocks_in_sector;
    int      first_block;
    int      key_a_dict_idx;
} sector_result_t;

static bool try_auth_key(nfc_iso14443a_data_t* card, uint8_t block,
                          mf_key_type_t key_type, const mf_classic_key_t* key)
{
    mf_classic_reset_auth();
    hb_nfc_err_t err = iso14443a_poller_reselect(card);
    if (err != HB_NFC_OK) return false;

    err = mf_classic_auth(block, key_type, key, card->uid);
    return (err == HB_NFC_OK);
}

static bool try_key_bytes(nfc_iso14443a_data_t* card, uint8_t block,
                           mf_key_type_t key_type, const uint8_t key_bytes[6])
{
    mf_classic_key_t k;
    memcpy(k.data, key_bytes, 6);
    return try_auth_key(card, block, key_type, &k);
}

void mf_classic_read_full(nfc_iso14443a_data_t* card)
{
    mf_classic_type_t type  = mf_classic_get_type(card->sak);
    int               nsect = mf_classic_get_sector_count(type);

    mfc_emu_card_data_init(&s_emu_card, card, type);
    s_emu_data_ready = false;

    const char* type_str = "1K (1024 bytes / 16 sectors)";
    int total_mem = 1024;
    if (type == MF_CLASSIC_MINI)  { type_str = "Mini (320 bytes / 5 sectors)"; total_mem = 320; }
    else if (type == MF_CLASSIC_4K) { type_str = "4K (4096 bytes / 40 sectors)"; total_mem = 4096; }

    const mf_known_card_t* known = mf_known_cards_match(
        card->sak, card->atqa, card->uid, card->uid_len);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "MIFARE Classic %s", type_str);
    ESP_LOGI(TAG, "Memory: %d bytes useful", total_mem);
    if (known) {
        ESP_LOGI(TAG, "Card ID: %s", known->name);
        ESP_LOGI(TAG, "         %s", known->hint);
    }
    ESP_LOGI(TAG, "");

    int sectors_read      = 0;
    int sectors_partial   = 0;
    int sectors_failed    = 0;
    int total_blocks_read = 0;
    int total_blocks      = 0;
    int keys_a_found      = 0;
    int keys_b_found      = 0;
    int data_blocks_used  = 0;

    uint8_t sect0_blk[4][16];
    bool    sect0_read = false;
    (void)sect0_read;

    int dict_count = mf_key_dict_count();

    /* Track which sectors need nested attack (key not found by dict/cache) */
    bool sector_key_a_failed[40] = { 0 };
    bool sector_key_b_failed[40] = { 0 };

    /* Track first known sector+key for use as nested attack source */
    int              nested_src_sector = -1;
    mf_classic_key_t nested_src_key;
    mf_key_type_t    nested_src_type = MF_KEY_A;

    static sector_result_t results[40];
    memset(results, 0, sizeof(results));

    for (int sect = 0; sect < nsect; sect++) {
        sector_result_t* res = &results[sect];
        res->blocks_in_sector = (sect < 32) ? 4 : 16;
        res->first_block      = (sect < 32) ? (sect * 4) : (128 + (sect - 32) * 16);
        int trailer_block     = res->first_block + res->blocks_in_sector - 1;
        res->key_a_dict_idx   = -1;
        total_blocks += res->blocks_in_sector;

        ESP_LOGI(TAG, "Sector %02d [block %03d..%03d]",
                 sect, res->first_block, trailer_block);

        /* --- Key A: try cache first, then dict --- */
        uint8_t cached_key[6];
        if (mf_key_cache_lookup(card->uid, card->uid_len, sect, MF_KEY_A, cached_key)) {
            if (try_key_bytes(card, (uint8_t)res->first_block, MF_KEY_A, cached_key)) {
                res->key_a_found = true;
                res->key_a_dict_idx = 0;
                memcpy(res->key_a, cached_key, 6);
                keys_a_found++;
                memcpy(s_emu_card.keys[sect].key_a, cached_key, 6);
                s_emu_card.keys[sect].key_a_known = true;
                prng_record_nonce(sect, mf_classic_get_last_nt());
            }
        }

        if (!res->key_a_found) {
            /* Priority: known-card hint keys (smaller set, faster) */
            if (known && known->hint_keys) {
                for (int k = 0; k < known->hint_key_count && !res->key_a_found; k++) {
                    if (try_key_bytes(card, (uint8_t)res->first_block, MF_KEY_A,
                                      known->hint_keys[k])) {
                        res->key_a_found = true;
                        res->key_a_dict_idx = 0;
                        memcpy(res->key_a, known->hint_keys[k], 6);
                        keys_a_found++;
                        memcpy(s_emu_card.keys[sect].key_a, known->hint_keys[k], 6);
                        s_emu_card.keys[sect].key_a_known = true;
                        prng_record_nonce(sect, mf_classic_get_last_nt());
                        mf_key_cache_store(card->uid, card->uid_len, sect, nsect,
                                           MF_KEY_A, known->hint_keys[k]);
                    }
                }
            }
            /* Fallback: full dictionary */
            for (int k = 0; k < dict_count && !res->key_a_found; k++) {
                uint8_t key_bytes[6];
                mf_key_dict_get(k, key_bytes);
                if (known && known->hint_keys) {
                    /* Skip keys already tried in the hint pass */
                    bool already = false;
                    for (int h = 0; h < known->hint_key_count; h++) {
                        if (memcmp(key_bytes, known->hint_keys[h], 6) == 0) {
                            already = true; break;
                        }
                    }
                    if (already) continue;
                }
                if (try_key_bytes(card, (uint8_t)res->first_block, MF_KEY_A, key_bytes)) {
                    res->key_a_found = true;
                    res->key_a_dict_idx = k;
                    memcpy(res->key_a, key_bytes, 6);
                    keys_a_found++;
                    memcpy(s_emu_card.keys[sect].key_a, key_bytes, 6);
                    s_emu_card.keys[sect].key_a_known = true;
                    prng_record_nonce(sect, mf_classic_get_last_nt());
                    mf_key_cache_store(card->uid, card->uid_len, sect, nsect,
                                       MF_KEY_A, key_bytes);
                }
            }
        }

        if (!res->key_a_found) {
            sector_key_a_failed[sect] = true;
        } else if (nested_src_sector < 0) {
            nested_src_sector = sect;
            memcpy(nested_src_key.data, res->key_a, 6);
            nested_src_type = MF_KEY_A;
        }

        /* --- Read blocks with Key A --- */
        if (res->key_a_found) {
            for (int b = 0; b < res->blocks_in_sector; b++) {
                hb_nfc_err_t err = mf_classic_read_block(
                    (uint8_t)(res->first_block + b), res->block_data[b]);
                if (err == HB_NFC_OK) {
                    res->blocks_read[b] = true;
                    total_blocks_read++;
                    memcpy(s_emu_card.blocks[res->first_block + b],
                           res->block_data[b], 16);
                }
            }
        }

        bool all_read = true;
        for (int b = 0; b < res->blocks_in_sector; b++) {
            if (!res->blocks_read[b]) { all_read = false; break; }
        }

        /* --- Key B: try cache first, then dict --- */
        if (mf_key_cache_lookup(card->uid, card->uid_len, sect, MF_KEY_B, cached_key)) {
            if (try_key_bytes(card, (uint8_t)res->first_block, MF_KEY_B, cached_key)) {
                res->key_b_found = true;
                memcpy(res->key_b, cached_key, 6);
                keys_b_found++;
                memcpy(s_emu_card.keys[sect].key_b, cached_key, 6);
                s_emu_card.keys[sect].key_b_known = true;
                if (nested_src_sector < 0) {
                    nested_src_sector = sect;
                    memcpy(nested_src_key.data, cached_key, 6);
                    nested_src_type = MF_KEY_B;
                }
            }
        }

        if (!res->key_b_found) {
            /* Helper macro: after finding Key B, read remaining blocks */
#define FOUND_KEY_B(kb)                                                          \
    do {                                                                         \
        res->key_b_found = true;                                                 \
        memcpy(res->key_b, (kb), 6);                                             \
        keys_b_found++;                                                          \
        memcpy(s_emu_card.keys[sect].key_b, (kb), 6);                           \
        s_emu_card.keys[sect].key_b_known = true;                               \
        mf_key_cache_store(card->uid, card->uid_len, sect, nsect, MF_KEY_B, (kb)); \
        if (nested_src_sector < 0) {                                             \
            nested_src_sector = sect;                                            \
            memcpy(nested_src_key.data, (kb), 6);                               \
            nested_src_type = MF_KEY_B;                                         \
        }                                                                        \
        if (!all_read) {                                                         \
            for (int _b = 0; _b < res->blocks_in_sector; _b++) {               \
                if (!res->blocks_read[_b]) {                                    \
                    hb_nfc_err_t _e = mf_classic_read_block(                    \
                        (uint8_t)(res->first_block + _b), res->block_data[_b]); \
                    if (_e == HB_NFC_OK) {                                      \
                        res->blocks_read[_b] = true;                            \
                        total_blocks_read++;                                     \
                        memcpy(s_emu_card.blocks[res->first_block + _b],        \
                               res->block_data[_b], 16);                        \
                    }                                                            \
                }                                                                \
            }                                                                    \
        }                                                                        \
    } while (0)

            /* Priority: known-card hint keys */
            if (known && known->hint_keys) {
                for (int k = 0; k < known->hint_key_count && !res->key_b_found; k++) {
                    if (try_key_bytes(card, (uint8_t)res->first_block, MF_KEY_B,
                                      known->hint_keys[k])) {
                        FOUND_KEY_B(known->hint_keys[k]);
                    }
                }
            }
            /* Fallback: full dictionary (skip already-tried hint keys) */
            for (int k = 0; k < dict_count && !res->key_b_found; k++) {
                uint8_t key_bytes[6];
                mf_key_dict_get(k, key_bytes);
                if (known && known->hint_keys) {
                    bool already = false;
                    for (int h = 0; h < known->hint_key_count; h++) {
                        if (memcmp(key_bytes, known->hint_keys[h], 6) == 0) {
                            already = true; break;
                        }
                    }
                    if (already) continue;
                }
                if (try_key_bytes(card, (uint8_t)res->first_block, MF_KEY_B, key_bytes)) {
                    FOUND_KEY_B(key_bytes);
                }
            }
#undef FOUND_KEY_B
        }

        if (!res->key_b_found) {
            sector_key_b_failed[sect] = true;
        }

        if (sect == 0 && res->blocks_read[0]) {
            for (int b = 0; b < 4; b++) {
                memcpy(sect0_blk[b], res->block_data[b], 16);
            }
            sect0_read = true;
        }

        char key_str[24];

        if (res->key_a_found) {
            hex_str_key(res->key_a, key_str, sizeof(key_str));
            ESP_LOGI(TAG, "Key A: %s", key_str);
        } else {
            ESP_LOGW(TAG, "Key A: -- -- -- -- -- -- (not found)");
        }

        if (res->key_b_found) {
            hex_str_key(res->key_b, key_str, sizeof(key_str));
            ESP_LOGI(TAG, "Key B: %s", key_str);
        } else {
            if (res->blocks_read[res->blocks_in_sector - 1]) {
                uint8_t c1, c2, c3;
                get_access_bits_for_block(res->block_data[res->blocks_in_sector - 1], 3, &c1, &c2, &c3);
                if (is_key_b_readable(c1, c2, c3)) {
                    uint8_t kb_from_trailer[6];
                    memcpy(kb_from_trailer, &res->block_data[res->blocks_in_sector - 1][10], 6);
                    hex_str_key(kb_from_trailer, key_str, sizeof(key_str));
                    ESP_LOGI(TAG, "Key B: %s (read from trailer used as data)", key_str);
                    memcpy(res->key_b, kb_from_trailer, 6);
                    res->key_b_found = true;
                    keys_b_found++;
                } else {
                    ESP_LOGI(TAG, "Key B: (not tested / protected)");
                }
            }
        }

        ESP_LOGI(TAG, "");

        char hex_buf[64], asc_buf[20];

        for (int b = 0; b < res->blocks_in_sector; b++) {
            int blk_num = res->first_block + b;
            bool is_trailer = (b == res->blocks_in_sector - 1);

            if (!res->blocks_read[b]) {
                ESP_LOGW(TAG, "[%03d] .. .. .. .. .. .. .. .. .. .. .. .. .. .. .. ..%s",
                         blk_num, is_trailer ? " trailer" : "");
                continue;
            }

            if (is_trailer) {
                char ka_display[24], kb_display[24], ac_str[16];

                if (res->key_a_found) {
                    hex_str_key(res->key_a, ka_display, sizeof(ka_display));
                } else {
                    snprintf(ka_display, sizeof(ka_display), "?? ?? ?? ?? ?? ??");
                }

                snprintf(ac_str, sizeof(ac_str), "%02X %02X %02X %02X",
                         res->block_data[b][6], res->block_data[b][7],
                         res->block_data[b][8], res->block_data[b][9]);

                if (res->key_b_found) {
                    hex_str_key(res->key_b, kb_display, sizeof(kb_display));
                } else {
                    hex_str_key(&res->block_data[b][10], kb_display, sizeof(kb_display));
                }

                ESP_LOGI(TAG, "[%03d] %s|%s|%s trailer",
                         blk_num, ka_display, ac_str, kb_display);

                if (verify_access_bits_parity(res->block_data[b])) {
                    ESP_LOGI(TAG, "Access bits valid:");
                } else {
                    ESP_LOGW(TAG, "Access bits INVALID (possible corruption!):");
                }

                for (int ab = 0; ab < res->blocks_in_sector; ab++) {
                    uint8_t c1, c2, c3;
                    get_access_bits_for_block(res->block_data[b], ab, &c1, &c2, &c3);
                    if (ab < res->blocks_in_sector - 1) {
                        ESP_LOGI(TAG, "Blk %d: C%d%d%d %s",
                                 ab, c1, c2, c3, access_cond_data_str(c1, c2, c3));
                    } else {
                        ESP_LOGI(TAG, "Trail: C%d%d%d %s",
                                 c1, c2, c3, access_cond_trailer_str(c1, c2, c3));
                    }
                }
            }
            else if (blk_num == 0) {
                hex_str(res->block_data[b], 16, hex_buf, sizeof(hex_buf));
                ESP_LOGI(TAG, "[%03d] %s manufacturer", blk_num, hex_buf);
            }
            else {
                hex_str(res->block_data[b], 16, hex_buf, sizeof(hex_buf));
                ascii_str(res->block_data[b], 16, asc_buf, sizeof(asc_buf));

                if (is_value_block(res->block_data[b])) {
                    int32_t val = decode_value_block(res->block_data[b]);
                    ESP_LOGI(TAG, "[%03d] %s value: %ld (addr %d)",
                             blk_num, hex_buf, (long)val, res->block_data[b][12]);
                    data_blocks_used++;
                }
                else if (is_block_empty(res->block_data[b])) {
                    ESP_LOGI(TAG, "[%03d] %s empty", blk_num, hex_buf);
                }
                else if (has_ascii_content(res->block_data[b])) {
                    ESP_LOGI(TAG, "[%03d] %s |%s|", blk_num, hex_buf, asc_buf);
                    data_blocks_used++;
                }
                else {
                    ESP_LOGI(TAG, "[%03d] %s", blk_num, hex_buf);
                    data_blocks_used++;
                }
            }
        }

        if (sect == 0 && res->blocks_read[0]) {
            analyze_manufacturer_block(res->block_data[0], card);

            if (res->blocks_read[1] && res->blocks_read[2]) {
                bool mad_key = (res->key_a_dict_idx == 1);
                check_mad(res->block_data[1], res->block_data[2], mad_key);
            }
        }

        bool all_read2 = true;
        bool any_read = false;
        for (int b = 0; b < res->blocks_in_sector; b++) {
            if (res->blocks_read[b]) any_read = true;
            else all_read2 = false;
        }

        if (all_read2) {
            ESP_LOGI(TAG, "Sector %02d: COMPLETE", sect);
            sectors_read++;
        } else if (any_read) {
            ESP_LOGW(TAG, "~ Sector %02d: PARTIAL", sect);
            sectors_partial++;
        } else {
            ESP_LOGW(TAG, "Sector %02d: FAILED", sect);
            sectors_failed++;
        }
        ESP_LOGI(TAG, "");
    }

    /* --- Nested authentication attack for sectors that resisted the dict --- */
    if (nested_src_sector >= 0) {
        bool nested_attempted = false;
        for (int sect = 0; sect < nsect; sect++) {
            if (!sector_key_a_failed[sect] && !sector_key_b_failed[sect]) continue;

            ESP_LOGI(TAG, "Nested attack sector %02d (src sector %02d)...",
                     sect, nested_src_sector);
            nested_attempted = true;

            mf_classic_key_t found_key;
            mf_key_type_t    found_type;
            hb_nfc_err_t nerr = mf_nested_attack(
                card,
                (uint8_t)nested_src_sector,
                &nested_src_key,
                nested_src_type,
                (uint8_t)sect,
                &found_key,
                &found_type);

            if (nerr != HB_NFC_OK) {
                ESP_LOGW(TAG, "Nested attack sector %02d: failed", sect);
                continue;
            }

            char key_str2[24];
            hex_str_key(found_key.data, key_str2, sizeof(key_str2));
            ESP_LOGI(TAG, "Nested attack sector %02d: found %s = %s",
                     sect, found_type == MF_KEY_A ? "Key A" : "Key B", key_str2);

            sector_result_t* res = &results[sect];
            if (found_type == MF_KEY_A) {
                sector_key_a_failed[sect] = false;
                res->key_a_found = true;
                memcpy(res->key_a, found_key.data, 6);
                keys_a_found++;
                memcpy(s_emu_card.keys[sect].key_a, found_key.data, 6);
                s_emu_card.keys[sect].key_a_known = true;
                mf_key_cache_store(card->uid, card->uid_len, sect, nsect,
                                   MF_KEY_A, found_key.data);
                mf_key_dict_add(found_key.data);
            } else {
                sector_key_b_failed[sect] = false;
                res->key_b_found = true;
                memcpy(res->key_b, found_key.data, 6);
                keys_b_found++;
                memcpy(s_emu_card.keys[sect].key_b, found_key.data, 6);
                s_emu_card.keys[sect].key_b_known = true;
                mf_key_cache_store(card->uid, card->uid_len, sect, nsect,
                                   MF_KEY_B, found_key.data);
                mf_key_dict_add(found_key.data);
            }

            /* Read the now-accessible blocks */
            iso14443a_poller_reselect(card);
            mf_classic_reset_auth();
            mf_classic_auth((uint8_t)res->first_block, found_type, &found_key, card->uid);
            for (int b = 0; b < res->blocks_in_sector; b++) {
                if (!res->blocks_read[b]) {
                    hb_nfc_err_t err = mf_classic_read_block(
                        (uint8_t)(res->first_block + b), res->block_data[b]);
                    if (err == HB_NFC_OK) {
                        res->blocks_read[b] = true;
                        total_blocks_read++;
                        memcpy(s_emu_card.blocks[res->first_block + b],
                               res->block_data[b], 16);
                    }
                }
            }

            bool all_now = true;
            for (int b = 0; b < res->blocks_in_sector; b++) {
                if (!res->blocks_read[b]) { all_now = false; break; }
            }
            if (all_now) {
                sectors_failed--;
                sectors_read++;
            } else {
                sectors_failed--;
                sectors_partial++;
            }
        }
        if (nested_attempted) {
            ESP_LOGI(TAG, "");
        }
    }

    mf_key_cache_save();
    prng_analyze();

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "READ RESULTS");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Sectors complete: %2d / %2d",
             sectors_read, nsect);
    if (sectors_partial > 0) {
        ESP_LOGW(TAG, "Sectors partial: %2d", sectors_partial);
    }
    if (sectors_failed > 0) {
        ESP_LOGW(TAG, "Sectors failed: %2d", sectors_failed);
    }
    ESP_LOGI(TAG, "Blocks read: %3d / %3d",
             total_blocks_read, total_blocks);
    ESP_LOGI(TAG, "Blocks with data: %3d (excluding trailers/empty)",
             data_blocks_used);
    ESP_LOGI(TAG, "Keys A found: %2d / %2d",
             keys_a_found, nsect);
    ESP_LOGI(TAG, "Keys B found: %2d / %2d",
             keys_b_found, nsect);

    if (s_prng.all_same && s_prng.count > 2) {
        ESP_LOGW(TAG, "");
        ESP_LOGW(TAG, "STATIC NONCE detected!");
        ESP_LOGW(TAG, "Card is likely a CLONE (Gen1a/Gen2).");
        ESP_LOGW(TAG, "Fixed PRNG = nt always 0x%08lX",
                 (unsigned long)s_prng.nonces[0]);
    } else if (s_prng.weak_prng) {
        ESP_LOGW(TAG, "");
        ESP_LOGW(TAG, "Weak PRNG detected: few unique nonces.");
        ESP_LOGW(TAG, "Possible card clone with simplified PRNG.");
    }

    if (sectors_read == nsect) {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "DUMP COMPLETE. All sectors read!");
        s_emu_data_ready = true;
    }

    ESP_LOGI(TAG, "");
}

void mf_classic_write_all(nfc_iso14443a_data_t* target, bool write_trailers)
{
    if (!s_emu_data_ready) {
        ESP_LOGW(TAG, "[WRITE] no card dump — run read first");
        return;
    }

    int nsect = s_emu_card.sector_count;
    int sectors_ok = 0, sectors_fail = 0, sectors_skip = 0;

    ESP_LOGI(TAG, "[WRITE] writing %d sectors%s...",
             nsect, write_trailers ? " (incl. trailers)" : "");

    for (int sect = 0; sect < nsect; sect++) {
        bool have_a = s_emu_card.keys[sect].key_a_known;
        bool have_b = s_emu_card.keys[sect].key_b_known;

        if (!have_a && !have_b) {
            ESP_LOGW(TAG, "[WRITE] sector %02d: no key — skipping", sect);
            sectors_skip++;
            continue;
        }

        const uint8_t* key    = have_a ? s_emu_card.keys[sect].key_a
                                       : s_emu_card.keys[sect].key_b;
        mf_key_type_t  ktype  = have_a ? MF_KEY_A : MF_KEY_B;

        int first  = (sect < 32) ? (sect * 4) : (128 + (sect - 32) * 16);
        int nblks  = (sect < 32) ? 4 : 16;
        int data_blocks = nblks - 1;          /* exclude trailer */

        /* Write data blocks */
        int written = mf_classic_write_sector(target, (uint8_t)sect,
                                               s_emu_card.blocks[first],
                                               key, ktype, false);
        if (written < 0) {
            ESP_LOGW(TAG, "[WRITE] sector %02d: error %d", sect, written);
            sectors_fail++;
            continue;
        }

        /* Optionally write trailer (keys + access bits) */
        if (write_trailers) {
            int trailer_blk = first + nblks - 1;
            mf_write_result_t wr = mf_classic_write(
                target, (uint8_t)trailer_blk,
                s_emu_card.blocks[trailer_blk],
                key, ktype, false, true);
            if (wr != MF_WRITE_OK) {
                ESP_LOGW(TAG, "[WRITE] sector %02d trailer: %s",
                         sect, mf_write_result_str(wr));
            }
        }

        ESP_LOGI(TAG, "[WRITE] sector %02d: %d/%d data blocks written",
                 sect, written, data_blocks);
        sectors_ok++;
    }

    ESP_LOGI(TAG, "[WRITE] done: %d ok  %d failed  %d skipped",
             sectors_ok, sectors_fail, sectors_skip);
}

void mfp_probe_and_dump(nfc_iso14443a_data_t* card)
{
    if (!card) return;

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "MIFARE Plus / ISO-DEP Card Probe");
    ESP_LOGI(TAG, "SAK: 0x%02X  ATQA: %02X %02X", card->sak, card->atqa[0], card->atqa[1]);
    ESP_LOGI(TAG, "");

    mfp_session_t session = { 0 };
    hb_nfc_err_t err = mfp_poller_init(card, &session);
    if (err != HB_NFC_OK) {
        ESP_LOGW(TAG, "RATS failed (%s) — not ISO-DEP or removed", hb_nfc_err_str(err));
        return;
    }

    if (session.dep.ats_len > 0) {
        nfc_log_hex("ATS:", session.dep.ats, session.dep.ats_len);
        ESP_LOGI(TAG, "FSC: %d  FWI: %d", session.dep.fsc, session.dep.fwi);
    }

    /* Probe: try MIFARE Plus SL3 FirstAuthenticate on sector 0 Key A (block 0x4000)
     * with all-zeros key. A genuine MFP SL3 will respond with 0x90 0x00 + 16-byte
     * challenge (EK_rndB). Any other status means not MFP SL3 or wrong key (expected). */
    const uint8_t zero_key[16] = { 0 };
    err = mfp_sl3_auth_first(&session, 0x4000, zero_key);

    if (err == HB_NFC_OK) {
        ESP_LOGI(TAG, "MIFARE Plus SL3 confirmed (sector 0 authenticated with zero key)");
        ESP_LOGI(TAG, "SES_ENC: %02X %02X %02X %02X ...",
                 session.ses_enc[0], session.ses_enc[1],
                 session.ses_enc[2], session.ses_enc[3]);
    } else {
        /* Key wrong or not MFP SL3 — still log the response code */
        ESP_LOGI(TAG, "MIFARE Plus SL3 probe: auth rejected (key wrong or not MFP SL3)");
        ESP_LOGI(TAG, "Card is likely DESFire, JCOP, or MFP with custom key");
    }

    ESP_LOGI(TAG, "");
}

#ifndef MF_ULC_TRY_DEFAULT_KEY
#define MF_ULC_TRY_DEFAULT_KEY 1
#endif

#if MF_ULC_TRY_DEFAULT_KEY
#ifndef MF_ULC_DEFAULT_KEY
#define MF_ULC_DEFAULT_KEY \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
#endif

static const uint8_t k_mf_ulc_default_key[16] = { MF_ULC_DEFAULT_KEY };
#endif

void mful_dump_card(nfc_iso14443a_data_t* card)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "MIFARE Ultralight / NTAG Read");
    ESP_LOGI(TAG, "");

    iso14443a_poller_reselect(card);
    uint8_t ver[8] = { 0 };
    int vlen = mful_get_version(ver);

    const char* tag_type = "Ultralight (classic)";
    int total_pages = 16;

    if (vlen >= 7) {
        nfc_log_hex("  VERSION:", ver, (size_t)vlen);
        uint8_t prod_type = ver[2];
        uint8_t prod_sub  = ver[3];
        uint8_t storage   = ver[6];

        if (prod_type == 0x03) {
            switch (storage) {
            case 0x03: tag_type = "Ultralight (64 bytes)"; total_pages = 16; break;
            case 0x0B: tag_type = "Ultralight EV1 (48 bytes)"; total_pages = 20; break;
            case 0x11: tag_type = "Ultralight EV1 (128 bytes)"; total_pages = 41; break;
            case 0x0E: tag_type = "Ultralight Nano (48 bytes)"; total_pages = 20; break;
            default: tag_type = "Ultralight (unknown size)"; break;
            }
        } else if (prod_type == 0x04) {
            switch (storage) {
            case 0x06: tag_type = "NTAG213 (144 bytes user)"; total_pages = 45; break;
            case 0x0E: tag_type = "NTAG215 (504 bytes user)"; total_pages = 135; break;
            case 0x12: tag_type = "NTAG216 (888 bytes user)"; total_pages = 231; break;
            case 0x0F: tag_type = "NTAG I2C 1K"; total_pages = 231; break;
            case 0x13: tag_type = "NTAG I2C 2K"; total_pages = 485; break;
            default:
                if (prod_sub == 0x02) {
                    tag_type = "NTAG (unknown size)";
                } else if (prod_sub == 0x05) {
                    tag_type = "NTAG I2C (unknown size)";
                }
                break;
            }
        }

        ESP_LOGI(TAG, "IC vendor: 0x%02X Type: 0x%02X Subtype: 0x%02X",
                 ver[1], prod_type, prod_sub);
        ESP_LOGI(TAG, "Major: %d Minor: %d Storage: 0x%02X", ver[4], ver[5], storage);
    }

    if (vlen < 7) {
#if MF_ULC_TRY_DEFAULT_KEY
        hb_nfc_err_t aerr = mful_ulc_auth(k_mf_ulc_default_key);
        if (aerr == HB_NFC_OK) {
            tag_type = "Ultralight C (3DES auth)";
            total_pages = 48;
            ESP_LOGI(TAG, "ULC auth OK (default key)");
        }
#endif
    }

    ESP_LOGI(TAG, "Tag: %s", tag_type);
    ESP_LOGI(TAG, "Total: %d pages (%d bytes)", total_pages, total_pages * 4);
    ESP_LOGI(TAG, "");

    iso14443a_poller_reselect(card);

    int pages_read = 0;
    char hex_buf[16], asc_buf[8];

    for (int pg = 0; pg < total_pages; pg += 4) {
        uint8_t raw[18] = { 0 };
        int rlen = mful_read_pages((uint8_t)pg, raw);

        if (rlen >= 16) {
            int batch = (total_pages - pg < 4) ? (total_pages - pg) : 4;
            for (int p = 0; p < batch; p++) {
                int page_num = pg + p;
                hex_str(&raw[p * 4], 4, hex_buf, sizeof(hex_buf));
                ascii_str(&raw[p * 4], 4, asc_buf, sizeof(asc_buf));

                const char* label = "";
                if (page_num <= 1) label = "  <- UID";
                else if (page_num == 2) label = "  <- Internal/Lock";
                else if (page_num == 3) label = "  <- OTP/CC";
                else if (page_num == 4) label = "  <- Data start";

                ESP_LOGI(TAG, "[%03d] %s |%s|%s", page_num, hex_buf, asc_buf, label);
                pages_read++;
            }
        } else {
            for (int p = 0; p < 4 && (pg + p) < total_pages; p++) {
                ESP_LOGW(TAG, "[%03d] .. .. .. .. |....| protected", pg + p);
            }
            break;
        }
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Pages read: %d / %d (%d%%)",
             pages_read, total_pages, pages_read * 100 / total_pages);
}

void t4t_dump_ndef(nfc_iso14443a_data_t* card)
{
    if (!card) return;

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "ISO-DEP / T4T NDEF Read");
    ESP_LOGI(TAG, "");

    iso14443a_poller_reselect(card);

    nfc_iso_dep_data_t dep = { 0 };
    hb_nfc_err_t err = iso_dep_rats(8, 0, &dep);
    if (err != HB_NFC_OK) {
        ESP_LOGW(TAG, "RATS failed: %s", hb_nfc_err_str(err));
        return;
    }

    t4t_cc_t cc = { 0 };
    err = t4t_read_cc(&dep, &cc);
    if (err != HB_NFC_OK) {
        ESP_LOGW(TAG, "CC read failed: %s", hb_nfc_err_str(err));
        return;
    }

    uint8_t ndef[512] = { 0 };
    size_t ndef_len = 0;
    err = t4t_read_ndef(&dep, &cc, ndef, sizeof(ndef), &ndef_len);
    if (err != HB_NFC_OK) {
        ESP_LOGW(TAG, "NDEF read failed: %s", hb_nfc_err_str(err));
        return;
    }

    ESP_LOGI(TAG, "NDEF length: %u bytes", (unsigned)ndef_len);
    size_t dump_len = ndef_len;
    if (dump_len > 256) dump_len = 256;
    if (dump_len > 0) nfc_log_hex("NDEF:", ndef, dump_len);
    if (ndef_len > dump_len) {
        ESP_LOGI(TAG, "NDEF truncated (total=%u bytes)", (unsigned)ndef_len);
    }
}
