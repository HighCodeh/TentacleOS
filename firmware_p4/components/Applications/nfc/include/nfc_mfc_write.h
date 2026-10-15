// Copyright (c) 2025 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef NFC_MFC_WRITE_H
#define NFC_MFC_WRITE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "highboy_nfc_compat.h"
#include "highboy_nfc_error.h"
#include "highboy_nfc_types.h"

#define NFC_MFC_WRITE_DATA_BLOCKS 47

typedef enum {
  NFC_MFC_WRITE_PHASE_FIND_KEY,
  NFC_MFC_WRITE_PHASE_WRITE,
  NFC_MFC_WRITE_PHASE_VERIFY,
} nfc_mfc_write_phase_t;

typedef struct {
  nfc_mfc_write_phase_t phase;
  int sector;
  int block;
  int blocks_written;
  int blocks_verified;
  int sectors_unlocked;
} nfc_mfc_write_progress_t;

typedef struct {
  hb_nfc_err_t err;
  nfc_iso14443a_data_t target;
  int blocks_expected;
  int blocks_written;
  int blocks_verified;
  int sectors_unlocked;
  int failed_block;
  bool cancelled;
} nfc_mfc_write_result_t;

typedef bool (*nfc_mfc_write_cancel_cb_t)(void *ctx);
typedef void (*nfc_mfc_write_progress_cb_t)(const nfc_mfc_write_progress_t *progress, void *ctx);

/**
 * @brief Copy the writable data blocks from a raw MIFARE Classic 1K dump.
 *
 * The manufacturer block (block 0), all sector trailers and all keys/access
 * bits are intentionally preserved on the target. Every written block is read
 * back and compared before it is counted as verified.
 */
hb_nfc_err_t nfc_mfc_write_data_1k(const char *dump_path,
                                   nfc_mfc_write_cancel_cb_t cancel_cb,
                                   nfc_mfc_write_progress_cb_t progress_cb,
                                   void *ctx,
                                   nfc_mfc_write_result_t *out);

/**
 * @brief Format selected data blocks as MAD1 + an NFC Forum Text record.
 *
 * The operation preserves block 0 and every sector trailer. It therefore
 * never changes the UID, keys, access bits or GPB. Only the MAD blocks and
 * the contiguous NFC data sectors required by the message are overwritten.
 */
hb_nfc_err_t nfc_mfc_write_text_1k(const char *text,
                                   nfc_mfc_write_cancel_cb_t cancel_cb,
                                   nfc_mfc_write_progress_cb_t progress_cb,
                                   void *ctx,
                                   nfc_mfc_write_result_t *out);

#ifdef __cplusplus
}
#endif

#endif // NFC_MFC_WRITE_H
