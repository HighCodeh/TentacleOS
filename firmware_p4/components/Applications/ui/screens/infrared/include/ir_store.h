// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

// Thin bridge between the infrared UI screens and the Service/ir library:
//   - /sdcard/ir/*.ir file storage (list / load / save / delete / rename)
//   - blocking TX wrappers that lazily bring the RMT TX channel up
//   - a single background RX capture task (ir_receive() blocks, so it cannot
//     run inside an lv_timer) with a queue hand-off polled from the UI thread.
// The screens keep their exact layout; only the data and effects become real.

#ifndef IR_STORE_H
#define IR_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/rmt_types.h"

#include "ir.h"
#include "ir_file.h"

/** @brief Max signal files surfaced by ir_store_list(). */
#define IR_STORE_MAX_ENTRIES 64
/** @brief Max length of a stored signal name (stem, no extension). */
#define IR_STORE_NAME_MAX IR_FILE_NAME_MAX
/** @brief Full path buffer length for a stored signal. */
#define IR_STORE_PATH_MAX 96

/**
 * @brief One .ir file on disk, summarized for list rows.
 */
typedef struct {
  char name[IR_STORE_NAME_MAX]; ///< File stem, no ".ir".
  char path[IR_STORE_PATH_MAX]; ///< Full path under /sdcard/ir.
  char proto[16];               ///< First signal's protocol name, or "RAW".
} ir_store_entry_t;

/**
 * @brief List *.ir files under /sdcard/ir.
 *
 * Missing directory is treated as empty (returns 0), not an error.
 *
 * @param[out] out  Destination array. Must not be NULL.
 * @param[in]  max  Capacity of @p out.
 * @return Number of entries written (0..max), or -1 on argument error.
 */
int ir_store_list(ir_store_entry_t *out, int max);

/**
 * @brief Read and parse a .ir file into @p file.
 *
 * Caller owns @p file: ir_file_init() before, ir_file_free() after.
 */
esp_err_t ir_store_load(const char *path, ir_file_t *file);

/** @brief True if /sdcard/ir/<name>.ir already exists. */
bool ir_store_exists(const char *name);

/** @brief Save one decoded frame as /sdcard/ir/<name>.ir (creates the dir). */
esp_err_t ir_store_save_data(const char *name, const ir_data_t *data);

/** @brief Save a raw symbol capture as /sdcard/ir/<name>.ir (creates the dir). */
esp_err_t
ir_store_save_raw(const char *name, const rmt_symbol_word_t *symbols, size_t count, uint32_t freq);

/** @brief Delete /sdcard/ir/<name>.ir. */
esp_err_t ir_store_delete(const char *name);

/** @brief Rename /sdcard/ir/<old_name>.ir to <new_name>.ir. */
esp_err_t ir_store_rename(const char *old_name, const char *new_name);

/**
 * @brief Build a stable file name for a decoded frame, e.g. "NEC_04_08".
 *
 * @param[in]  data  Decoded frame. Must not be NULL.
 * @param[out] buf   Destination. Must not be NULL.
 * @param[in]  cap   Capacity of @p buf.
 */
void ir_store_name_for_data(const ir_data_t *data, char *buf, size_t cap);

/**
 * @brief Pick the next free "<prefix>_NNN" name that does not exist yet.
 */
void ir_store_next_free(const char *prefix, char *buf, size_t cap);

/** @brief Transmit a decoded frame (brings TX up first). */
esp_err_t ir_store_send_data(const ir_data_t *data);

/** @brief Transmit a parsed/raw signal from a loaded file (brings TX up first). */
esp_err_t ir_store_send_signal(const ir_signal_t *signal);

/** @brief Transmit a raw symbol buffer (brings TX up first). */
esp_err_t ir_store_send_raw(const rmt_symbol_word_t *symbols, size_t count, uint32_t freq);

/**
 * @brief Send every signal in @p file whose name matches @p name (case-insensitive).
 *
 * Universal-remote brute force: one button maps to many brand codes. Blocking —
 * call at most a few per UI tick.
 *
 * @return Number of signals transmitted.
 */
int ir_store_send_named(const ir_file_t *file, const char *name);

/** @brief Capture task status. */
typedef enum {
  IR_CAP_IDLE = 0, ///< No capture started (or reset).
  IR_CAP_BUSY,     ///< Capture task running.
  IR_CAP_GOT,      ///< A frame arrived (decoded, or raw-only with UNKNOWN protocol).
  IR_CAP_TIMEOUT,  ///< Window elapsed with no frame.
  IR_CAP_ERROR,    ///< RX init or driver error.
} ir_cap_status_t;

/**
 * @brief Start a one-shot RX capture in a background task (brings RX up first).
 *
 * No-op returning ESP_ERR_INVALID_STATE if a capture is already running.
 */
esp_err_t ir_capture_start(uint32_t timeout_ms);

/**
 * @brief Poll the capture. When IR_CAP_GOT and @p out != NULL, copies the frame.
 */
ir_cap_status_t ir_capture_poll(ir_data_t *out);

/** @brief True if the captured frame decoded to a known protocol (not raw-only). */
bool ir_capture_decoded(void);

/** @brief Return to IDLE if no task is running (call on screen enter/exit). */
void ir_capture_reset(void);

#ifdef __cplusplus
}
#endif

#endif // IR_STORE_H
