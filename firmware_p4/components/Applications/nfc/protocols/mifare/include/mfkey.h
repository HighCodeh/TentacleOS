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
#ifndef MFKEY_H
#define MFKEY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Two independent reader authentication transcripts, in on-air word order. */
typedef struct {
  uint32_t uid;
  uint32_t nt0, nr0, ar0;
  uint32_t nt1, nr1, ar1;
} mfkey_pair_t;

typedef enum {
  MFKEY_RESULT_FOUND = 0,
  MFKEY_RESULT_NOT_FOUND,
  MFKEY_RESULT_INVALID,
  MFKEY_RESULT_NO_MEMORY,
  MFKEY_RESULT_CANCELLED,
  MFKEY_RESULT_CAPACITY,
  MFKEY_RESULT_COUNT
} mfkey_result_t;

/** Runs on the caller task. Return false to cancel; work is not a percentage. */
typedef bool (*mfkey_progress_cb_t)(uint32_t work, void *context);

/** Offline recovery; no RF, storage writes or key logging. Caller owns inputs.
 * Uses two bounded PSRAM tables on ESP (16 MiB total); no internal-RAM fallback.
 * Clears out_key on failure and verifies candidates against both transcripts.
 */
mfkey_result_t mfkey_recover(const mfkey_pair_t *pair,
                             uint64_t *out_key,
                             mfkey_progress_cb_t progress,
                             void *context);

/** Compatibility wrapper for two reader transcripts; this is not a nested attack. */
bool mfkey32(uint32_t uid,
             uint32_t nt0,
             uint32_t nr0,
             uint32_t ar0,
             uint32_t nt1,
             uint32_t nr1,
             uint32_t ar1,
             uint64_t *key);

#ifdef __cplusplus
}
#endif
#endif
