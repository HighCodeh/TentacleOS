/**
 * @file mf_classic_writer.h
 * @brief MIFARE Classic block write with Crypto1 authentication.
 *
 * Flow to write a data block:
 *  1. Reselect (field cycle + REQA + anticoll + SELECT)
 *  2. Auth (Crypto1, Key A or B)
 *  3. WRITE cmd (0xA0 + block) wait for ACK (0x0A)
 *  4. Send 16 data bytes and wait for ACK (0x0A)
 *
 * WARNING: Never write block 0 (manufacturer) and use extreme care with
 * trailers; wrong access bits can lock the sector permanently.
 */
#ifndef MF_CLASSIC_WRITER_H
#define MF_CLASSIC_WRITER_H

#include <stdint.h>
#include <stdbool.h>
#include "highboy_nfc_types.h"
#include "highboy_nfc_error.h"
#include "mf_classic.h"

typedef enum {
    MF_WRITE_OK           = 0,
    MF_WRITE_ERR_RESELECT,   
    MF_WRITE_ERR_AUTH,       
    MF_WRITE_ERR_CMD_NACK,   
    MF_WRITE_ERR_DATA_NACK,  
    MF_WRITE_ERR_VERIFY,     
    MF_WRITE_ERR_PROTECTED,  
    MF_WRITE_ERR_PARAM,      
} mf_write_result_t;

const char* mf_write_result_str(mf_write_result_t r);

/**
 * @brief Access bits (C1/C2/C3) for groups 0..3.
 *
 * Group 0..3 mapping:
 *  - Mini/1K: blocks 0,1,2,3 (trailer = group 3)
 *  - 4K (sectors 32-39): groups 0-2 cover 5 blocks each,
 *    group 3 is the trailer (block 15).
 */
typedef struct {
    uint8_t c1[4];
    uint8_t c2[4];
    uint8_t c3[4];
} mf_classic_access_bits_t;

/**
 * @brief Write 16 bytes to a block already authenticated (active session).
 *
 * Call this function IMMEDIATELY after mf_classic_auth();
 * the Crypto1 session must be active.
 *
 * @param block Absolute block number (Mini: 0-19, 1K: 0-63, 4K: 0-255).
 * @param data 16 bytes to write.
 * @return MF_WRITE_OK or error code.
 */
mf_write_result_t mf_classic_write_block_raw(uint8_t block,
                                               const uint8_t data[16]);

/**
 * @brief Authenticate and write a data block (full flow).
 *
 * Runs reselect, auth, write, and optional verify.
 * Rejects block 0 and trailers unless allow_special is true.
 *
 * @param card Card data (UID required for auth).
 * @param block Absolute block to write.
 * @param data 16 bytes to write.
 * @param key 6-byte key.
 * @param key_type MF_KEY_A or MF_KEY_B.
 * @param verify If true, reads the block after write to confirm.
 * @param allow_special If true, allows writing trailers (dangerous!).
 * @return mf_write_result_t
 */
mf_write_result_t mf_classic_write(nfc_iso14443a_data_t* card,
                                    uint8_t               block,
                                    const uint8_t         data[16],
                                    const uint8_t         key[6],
                                    mf_key_type_t         key_type,
                                    bool                  verify,
                                    bool                  allow_special);

/**
 * @brief Write a full sector (data blocks only, excludes trailer).
 *
 * Iterates over data blocks in the sector (does not write the trailer).
 * Uses only ONE reselect + auth per sector (efficient).
 *
 * @param card Card data.
 * @param sector Sector number (Mini: 0-4, 1K: 0-15, 4K: 0-39).
 * @param data Buffer with (blocks_in_sector - 1) * 16 bytes.
 *  For Mini/1K: 3 blocks x 16 = 48 bytes per sector.
 *  For 4K (sectors 32-39): 15 blocks x 16 = 240 bytes.
 * @param key 6-byte key.
 * @param key_type MF_KEY_A or MF_KEY_B.
 * @param verify Verifies each block after write.
 * @return Number of blocks written successfully, or negative on fatal error.
 */
int mf_classic_write_sector(nfc_iso14443a_data_t* card,
                             uint8_t               sector,
                             const uint8_t*        data,
                             const uint8_t         key[6],
                             mf_key_type_t         key_type,
                             bool                  verify);

/**
 * @brief Encode access bits (C1/C2/C3) into the 3 trailer bytes.
 *
 * Generates bytes 6-8 with the correct inversions/parity.
 * Returns false if any bit is not 0/1.
 */
bool mf_classic_access_bits_encode(const mf_classic_access_bits_t* ac,
                                    uint8_t                         out_access_bits[3]);

/**
 * @brief Validate parity/inversions of the 3 access bits bytes.
 *
 * @param access_bits 3 bytes (bytes 6-8 of the trailer).
 * @return true if the bits are consistent.
 */
bool mf_classic_access_bits_valid(const uint8_t access_bits[3]);

/**
 * @brief Build a safe trailer from keys and access bits (C1/C2/C3).
 *
 * Computes bytes 6-8 automatically and validates inversions.
 *
 * @param key_a 6 bytes of Key A.
 * @param key_b 6 bytes of Key B.
 * @param ac Access bits C1/C2/C3 per group (0..3).
 * @param gpb General Purpose Byte (byte 9).
 * @param out_trailer Output buffer of 16 bytes.
 * @return true if the trailer was built successfully.
 */
bool mf_classic_build_trailer_safe(const uint8_t              key_a[6],
                                    const uint8_t              key_b[6],
                                    const mf_classic_access_bits_t* ac,
                                    uint8_t                    gpb,
                                    uint8_t                    out_trailer[16]);

/**
 * @brief Build a trailer from keys and access bits (bytes 6-8).
 *
 * Does NOT validate parity. Use mf_classic_build_trailer_safe() to build
 * and validate the access bits automatically.
 *
 * @param key_a 6 bytes of Key A.
 * @param key_b 6 bytes of Key B.
 * @param access_bits 3 bytes of access bits (bytes 6, 7, 8 of trailer).
 *  Pass NULL to use safe default bits.
 * @param out_trailer Output buffer of 16 bytes.
 */
void mf_classic_build_trailer(const uint8_t  key_a[6],
                               const uint8_t  key_b[6],
                               const uint8_t  access_bits[3],
                               uint8_t        out_trailer[16]);

extern const uint8_t MF_ACCESS_BITS_DEFAULT[3];

extern const uint8_t MF_ACCESS_BITS_READ_ONLY[3];

#endif 
