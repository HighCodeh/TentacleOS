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

#ifndef IR_FILE_H
#define IR_FILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ir.h"

/** @brief Maximum length of a signal name, including null terminator. */
#define IR_FILE_NAME_MAX 32

/** @brief Maximum length of a protocol name string, including null terminator. */
#define IR_FILE_PROTO_NAME_MAX 32

/** @brief Maximum input size. Lines are read in full using a PSRAM buffer. */
#define IR_FILE_CONTENT_MAX (512U * 1024U)

/** @brief Bound RAW storage to 32 KiB per signal (up to 16 KiB for TX symbols). */
#define IR_FILE_RAW_MAX_PULSES 8192U

/** @brief Current RMT TX limits; oversized durations are rejected, never truncated. */
#define IR_FILE_RAW_MAX_DURATION_US  32767U
#define IR_FILE_RAW_MAX_TOTAL_US     900000U
#define IR_FILE_RAW_MIN_FREQUENCY_HZ 10000U
#define IR_FILE_RAW_MAX_FREQUENCY_HZ 100000U

/** @brief Initial capacity of the signals array before any growth. */
#define IR_FILE_INITIAL_CAP 8

/**
 * @brief A single IR signal, either decoded or raw.
 *
 * Buffer ownership: raw is allocated on parse or ir_file_add_raw, freed by ir_file_free.
 */
typedef struct {
  char name[IR_FILE_NAME_MAX];
  bool is_raw;
  ir_data_t data;
  uint32_t frequency;
  uint32_t *raw;
  size_t raw_count;
} ir_signal_t;

/**
 * @brief Container for a collection of IR signals parsed from or written to a file.
 *
 * Buffer ownership: signals is grown on demand and freed by ir_file_free.
 */
typedef struct {
  ir_signal_t *signals;
  size_t count;
  size_t capacity;
} ir_file_t;

/**
 * @brief Configuration for ir_file_add_raw.
 */
typedef struct {
  const char *name;
  const rmt_symbol_word_t *symbols;
  size_t count;
  uint32_t freq;
} ir_file_add_raw_cfg_t;

/**
 * @brief Initialize an ir_file_t to an empty state.
 *
 * @param[out] file  File to initialize. Must not be NULL.
 */
void ir_file_init(ir_file_t *file);

/**
 * @brief Free all signals and internal buffers owned by the file.
 *
 * @param[in,out] file  File to free. Must not be NULL.
 */
void ir_file_free(ir_file_t *file);

/**
 * @brief Parse a Flipper Zero IR file from a null-terminated string.
 *
 * Appends parsed signals to @p file. Caller must call ir_file_init before
 * the first parse and ir_file_free when done. On error, signals appended by this
 * call are freed and pre-existing signals are preserved. RAW lines are never
 * truncated; each signal is limited to IR_FILE_RAW_MAX_PULSES timings.
 *
 * @param[in]     content  Null-terminated file content. Must not be NULL.
 * @param[in,out] file     Destination file. Must not be NULL.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if any pointer is NULL
 *   - ESP_ERR_NO_MEM if allocation fails
 *   - ESP_ERR_INVALID_SIZE if content or a RAW signal exceeds the documented limit
 *   - ESP_ERR_INVALID_ARG if a RAW timing or signal field is malformed
 */
esp_err_t ir_file_parse(const char *content, ir_file_t *file);

/**
 * @brief Serialize all signals in the file to a Flipper Zero IR format string.
 *
 * Writes into @p buf up to @p buf_size bytes. Output is truncated if the
 * buffer is too small — no null terminator is guaranteed beyond @p buf_size.
 *
 * @param[in]  file      File to serialize. Must not be NULL.
 * @param[out] buf       Destination buffer. Must not be NULL.
 * @param[in]  buf_size  Capacity of @p buf in bytes. Must be greater than 0.
 *
 * @return Number of bytes written (excluding null terminator).
 */
size_t ir_file_to_string(const ir_file_t *file, char *buf, size_t buf_size);

/**
 * @brief Find a signal by name.
 *
 * @param[in] file  File to search. Must not be NULL.
 * @param[in] name  Signal name to look up. Must not be NULL.
 *
 * @return Pointer to the matching signal, or NULL if not found.
 */
ir_signal_t *ir_file_find(const ir_file_t *file, const char *name);

/**
 * @brief Transmit a signal over IR.
 *
 * Dispatches to ir_send_raw for raw signals or ir_send for decoded signals.
 * RAW transmission accepts positive durations through 32767 us, total duration
 * through 900000 us, and carriers from 10 to 100 kHz. These limits preserve the
 * current driver's 15-bit durations and one-second completion timeout.
 *
 * @param[in] signal  Signal to transmit. Must not be NULL.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if signal is NULL
 *   - Other ESP_ERR codes from the IR driver
 */
esp_err_t ir_file_send(const ir_signal_t *signal);

/**
 * @brief Add a decoded IR signal to the file.
 *
 * @param[in,out] file  Destination file. Must not be NULL.
 * @param[in]     name  Signal name. Must not be NULL.
 * @param[in]     data  Decoded IR data to store. Must not be NULL.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if any pointer is NULL
 *   - ESP_ERR_NO_MEM if allocation fails
 */
esp_err_t ir_file_add_parsed(ir_file_t *file, const char *name, const ir_data_t *data);

/**
 * @brief Add a raw RMT symbol sequence to the file.
 *
 * @param[in,out] file  Destination file. Must not be NULL.
 * @param[in]     cfg   Add configuration. Must not be NULL. All fields must be valid.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if any pointer is NULL or cfg->count is 0
 *   - ESP_ERR_NO_MEM if allocation fails
 */
esp_err_t ir_file_add_raw(ir_file_t *file, const ir_file_add_raw_cfg_t *cfg);

#ifdef __cplusplus
}
#endif

#endif // IR_FILE_H
