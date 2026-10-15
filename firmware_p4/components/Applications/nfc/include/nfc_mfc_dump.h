// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

#ifndef NFC_MFC_DUMP_H
#define NFC_MFC_DUMP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "highboy_nfc_compat.h"
#include "highboy_nfc_error.h"
#include "highboy_nfc_types.h"

#define NFC_MFC1K_SECTORS  16
#define NFC_MFC1K_BLOCKS   64
#define NFC_MFC_BLOCK_SIZE 16
#define NFC_MFC_KEY_SIZE   6
#define NFC_MFC_PATH_MAX   128

typedef enum {
  NFC_MFC_DUMP_PHASE_KEYS,
  NFC_MFC_DUMP_PHASE_BLOCKS,
  NFC_MFC_DUMP_PHASE_SAVE,
} nfc_mfc_dump_phase_t;

typedef struct {
  nfc_mfc_dump_phase_t phase;
  int sector;
  int blocks_read;
  int keys_found;
} nfc_mfc_dump_progress_t;

typedef struct {
  hb_nfc_err_t err;
  uint8_t blocks[NFC_MFC1K_BLOCKS][NFC_MFC_BLOCK_SIZE];
  bool block_read[NFC_MFC1K_BLOCKS];
  uint8_t key_a[NFC_MFC1K_SECTORS][NFC_MFC_KEY_SIZE];
  uint8_t key_b[NFC_MFC1K_SECTORS][NFC_MFC_KEY_SIZE];
  bool key_a_found[NFC_MFC1K_SECTORS];
  bool key_b_found[NFC_MFC1K_SECTORS];
  int blocks_read;
  int sectors_complete;
  int keys_a_found;
  int keys_b_found;
  bool cancelled;
  bool saved;
  bool emulation_ready;
  char bin_path[NFC_MFC_PATH_MAX];
  char keys_path[NFC_MFC_PATH_MAX];
} nfc_mfc_dump_result_t;

typedef bool (*nfc_mfc_dump_cancel_cb_t)(void *ctx);
typedef void (*nfc_mfc_dump_progress_cb_t)(const nfc_mfc_dump_progress_t *progress, void *ctx);

/**
 * @brief Read a MIFARE Classic 1K using cached/dictionary keys only.
 *
 * This routine never sends a write command and never runs nested attacks. It
 * owns the RF poller for the duration of the operation and always turns the
 * field off before returning.
 */
hb_nfc_err_t nfc_mfc_dump_read_1k(nfc_iso14443a_data_t *card,
                                  nfc_mfc_dump_cancel_cb_t cancel_cb,
                                  nfc_mfc_dump_progress_cb_t progress_cb,
                                  void *ctx,
                                  nfc_mfc_dump_result_t *out);

/**
 * @brief Save a complete 1K dump and its key report without overwriting files.
 *
 * The binary is a raw 1024-byte image suitable for Proxmark3 `hf mf eload`.
 * Known keys are reconstructed into each sector trailer.
 */
esp_err_t nfc_mfc_dump_save(const nfc_iso14443a_data_t *card, nfc_mfc_dump_result_t *dump);

#ifdef __cplusplus
}
#endif

#endif // NFC_MFC_DUMP_H
