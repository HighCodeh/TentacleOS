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

/* State-recovery construction derived from CRAPTO1 (bla, 2008-2014;
 * Proxmark3 contributors), also used by Flipper's MFKey32v2. GPL-3.0-or-later.
 * Adaptation: bounded index ranges, streamed candidate verification, explicit
 * PSRAM allocation and cooperative cancellation. See docs/nfc/README.md. */
#include "mfkey.h"

#include <stdlib.h>
#include "crypto1.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char *TAG = "NFC_MFKEY";
#endif

#define MK_BIT(x, n)        (((x) >> (n)) & 1U)
#define MK_BEBIT(x, n)      MK_BIT((x), (n) ^ 24)
#define MK_LFP_ODD          0x29CE5CU
#define MK_LFP_EVEN         0x870804U
#define MK_STATE_MASK       0xFFFFFFU
#define MK_PREFIX_MASK      0xFF000000U
#define MK_FILTER_BITS      20
#define MK_TABLE_CAPACITY   (1U << 21)
#define MK_CHECK_INTERVAL   4096U
#define MK_PRNG_ADVANCE     64U
#define MK_SIMPLE_ROUNDS    4
#define MK_REMAINING_ROUNDS 11
#define MK_HALF_STATE_BITS  24
#define MK_WORD_BITS        32

typedef struct {
  int32_t head, tail;
  uint32_t stream;
} mfkey_slice_t;

typedef struct {
  const mfkey_pair_t *pair;
  uint32_t *odd, *even;
  uint64_t key;
  uint32_t work;
  mfkey_progress_cb_t progress;
  void *context;
  mfkey_result_t result;
} mfkey_search_t;

static uint32_t filter(uint32_t in) {
  uint32_t value = (0xf22c0U >> (in & 0xfU)) & 16U;
  value |= (0x6c9c0U >> ((in >> 4) & 0xfU)) & 8U;
  value |= (0x3c8b0U >> ((in >> 8) & 0xfU)) & 4U;
  value |= (0x1e458U >> ((in >> 12) & 0xfU)) & 2U;
  value |= (0x0d938U >> ((in >> 16) & 0xfU)) & 1U;
  return MK_BIT(0xEC57E80AU, value);
}

static uint32_t parity(uint32_t value) {
  return (uint32_t)__builtin_parity(value);
}

static bool checkpoint(mfkey_search_t *search) {
  if (search->result != MFKEY_RESULT_NOT_FOUND)
    return false;
  if ((++search->work % MK_CHECK_INTERVAL) != 0)
    return true;
  if (search->progress != NULL && !search->progress(search->work, search->context)) {
    search->result = MFKEY_RESULT_CANCELLED;
    return false;
  }
#ifdef ESP_PLATFORM
  vTaskDelay(1);
#endif
  return true;
}

static uint32_t contribution(uint32_t value, bool is_odd) {
  uint32_t prefix = value >> 25;
  const uint32_t mask1 = is_odd ? (MK_LFP_EVEN << 1) | 1U : MK_LFP_ODD;
  const uint32_t mask2 = is_odd ? MK_LFP_ODD << 1 : (MK_LFP_EVEN << 1) | 1U;
  prefix = (prefix << 1) | parity(value & mask1);
  prefix = (prefix << 1) | parity(value & mask2);
  return (prefix << 24) | (value & MK_STATE_MASK);
}

/* New branches replace the next input, which is moved to the unprocessed tail.
 * No one-before-start pointer or speculative read after the current tail. */
static bool extend(mfkey_search_t *search, mfkey_slice_t *slice, bool is_odd, bool has_feedback) {
  uint32_t *data = is_odd ? search->odd : search->even;
  int32_t index = slice->head;
  const uint32_t bit = slice->stream & 1U;
  while (index <= slice->tail) {
    if (!checkpoint(search))
      return false;
    uint32_t value = data[index] << 1;
    const uint32_t zero = filter(value), one = filter(value | 1U);
    if (zero != one) {
      value |= zero ^ bit;
      data[index++] = has_feedback ? contribution(value, is_odd) : value;
    } else if (zero == bit) {
      if ((uint32_t)(slice->tail + 1) >= MK_TABLE_CAPACITY) {
        search->result = MFKEY_RESULT_CAPACITY;
        return false;
      }
      const uint32_t displaced = index < slice->tail ? data[index + 1] : 0;
      data[++slice->tail] = displaced;
      data[index++] = has_feedback ? contribution(value, is_odd) : value;
      data[index++] = has_feedback ? contribution(value | 1U, is_odd) : value | 1U;
    } else {
      data[index] = data[slice->tail--];
    }
  }
  return true;
}

/* Recurse only into the smaller partition: stack depth is logarithmic. */
static bool sort_slice(mfkey_search_t *search, uint32_t *data, mfkey_slice_t slice) {
  while (slice.head < slice.tail) {
    int32_t left = slice.head, right = slice.tail;
    const uint32_t pivot = data[left + (right - left) / 2];
    while (left <= right) {
      while (left <= slice.tail && data[left] < pivot) {
        left++;
        if (!checkpoint(search))
          return false;
      }
      while (right >= slice.head && data[right] > pivot) {
        right--;
        if (!checkpoint(search))
          return false;
      }
      if (left <= right) {
        const uint32_t saved = data[left];
        data[left++] = data[right];
        data[right--] = saved;
      }
      if (!checkpoint(search))
        return false;
    }
    if (right - slice.head < slice.tail - left) {
      if (slice.head < right && !sort_slice(search, data, (mfkey_slice_t){slice.head, right, 0}))
        return false;
      slice.head = left;
    } else {
      if (left < slice.tail && !sort_slice(search, data, (mfkey_slice_t){left, slice.tail, 0}))
        return false;
      slice.tail = right;
    }
  }
  return true;
}

static int32_t group_start(const uint32_t *data, mfkey_slice_t slice) {
  const uint32_t prefix = data[slice.tail] & MK_PREFIX_MASK;
  while (slice.head < slice.tail) {
    int32_t middle = slice.head + (slice.tail - slice.head) / 2;
    if ((data[middle] & MK_PREFIX_MASK) < prefix)
      slice.head = middle + 1;
    else
      slice.tail = middle;
  }
  return slice.head;
}

static void rollback_word(crypto1_state_t *state, uint32_t input, bool encrypted) {
  for (int32_t bit = MK_WORD_BITS - 1; bit >= 0; bit--) {
    uint32_t saved = state->odd & MK_STATE_MASK;
    state->odd = state->even;
    state->even = saved;
    uint32_t feed = state->even & 1U;
    state->even >>= 1;
    feed ^= state->even & MK_LFP_EVEN;
    feed ^= state->odd & MK_LFP_ODD;
    feed ^= MK_BEBIT(input, bit);
    if (encrypted)
      feed ^= filter(state->odd);
    state->even |= parity(feed) << (MK_HALF_STATE_BITS - 1);
  }
}

static bool verify_candidate(mfkey_search_t *search, crypto1_state_t state) {
  const mfkey_pair_t *pair = search->pair;
  rollback_word(&state, 0, false);
  rollback_word(&state, pair->nr0, true);
  rollback_word(&state, pair->uid ^ pair->nt0, false);
  uint64_t key = 0;
  for (int32_t bit = MK_HALF_STATE_BITS - 1; bit >= 0; bit--) {
    key = (key << 1) | MK_BIT(state.odd, bit ^ 3);
    key = (key << 1) | MK_BIT(state.even, bit ^ 3);
  }
  const uint32_t nt[] = {pair->nt0, pair->nt1};
  const uint32_t nr[] = {pair->nr0, pair->nr1};
  const uint32_t ar[] = {pair->ar0, pair->ar1};
  for (size_t index = 0; index < sizeof(nt) / sizeof(nt[0]); index++) {
    crypto1_init(&state, key);
    crypto1_word(&state, pair->uid ^ nt[index], 0);
    crypto1_word(&state, nr[index], 1);
    if ((crypto1_word(&state, 0, 0) ^ crypto1_prng_successor(nt[index], MK_PRNG_ADVANCE)) !=
        ar[index])
      return false;
  }
  search->key = key;
  search->result = MFKEY_RESULT_FOUND;
  return true;
}

static void
recover(mfkey_search_t *search, mfkey_slice_t odd, mfkey_slice_t even, int32_t remaining) {
  if (remaining == -1) {
    for (int32_t e = even.head; e <= even.tail; e++) {
      const uint32_t value = (search->even[e] << 1) ^ parity(search->even[e] & MK_LFP_EVEN);
      for (int32_t o = odd.head; o <= odd.tail; o++) {
        if (!checkpoint(search))
          return;
        crypto1_state_t state = {.even = search->odd[o],
                                 .odd = value ^ parity(search->odd[o] & MK_LFP_ODD)};
        if (verify_candidate(search, state))
          return;
      }
    }
    return;
  }
  for (int32_t round = 0; round < MK_SIMPLE_ROUNDS && remaining-- != 0; round++) {
    odd.stream >>= 1;
    even.stream >>= 1;
    if (!extend(search, &odd, true, true) || odd.head > odd.tail ||
        !extend(search, &even, false, true) || even.head > even.tail)
      return;
  }
  if (!sort_slice(search, search->odd, odd) || !sort_slice(search, search->even, even))
    return;
  while (odd.head <= odd.tail && even.head <= even.tail && checkpoint(search)) {
    const uint32_t odd_prefix = search->odd[odd.tail] & MK_PREFIX_MASK;
    const uint32_t even_prefix = search->even[even.tail] & MK_PREFIX_MASK;
    if (odd_prefix == even_prefix) {
      const int32_t odd_start = group_start(search->odd, odd);
      const int32_t even_start = group_start(search->even, even);
      recover(search,
              (mfkey_slice_t){odd_start, odd.tail, odd.stream},
              (mfkey_slice_t){even_start, even.tail, even.stream},
              remaining);
      odd.tail = odd_start - 1;
      even.tail = even_start - 1;
    } else if (odd_prefix > even_prefix) {
      odd.tail = group_start(search->odd, odd) - 1;
    } else {
      even.tail = group_start(search->even, even) - 1;
    }
  }
}

mfkey_result_t mfkey_recover(const mfkey_pair_t *pair,
                             uint64_t *out_key,
                             mfkey_progress_cb_t progress,
                             void *context) {
  if (out_key == NULL)
    return MFKEY_RESULT_INVALID;
  *out_key = 0;
  if (pair == NULL || (pair->nt0 == pair->nt1 && pair->nr0 == pair->nr1))
    return MFKEY_RESULT_INVALID;
  if (progress != NULL && !progress(0, context))
    return MFKEY_RESULT_CANCELLED;
  mfkey_search_t search = {
      .pair = pair, .progress = progress, .context = context, .result = MFKEY_RESULT_NOT_FOUND};
#ifdef ESP_PLATFORM
  search.odd =
      heap_caps_malloc(MK_TABLE_CAPACITY * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  search.even =
      heap_caps_malloc(MK_TABLE_CAPACITY * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
  search.odd = malloc(MK_TABLE_CAPACITY * sizeof(uint32_t));
  search.even = malloc(MK_TABLE_CAPACITY * sizeof(uint32_t));
#endif
  if (search.odd == NULL || search.even == NULL) {
#ifdef ESP_PLATFORM
    ESP_LOGE(TAG, "Cannot allocate recovery tables in PSRAM");
#endif
    search.result = MFKEY_RESULT_NO_MEMORY;
    goto cleanup;
  }
  uint32_t stream = pair->ar0 ^ crypto1_prng_successor(pair->nt0, MK_PRNG_ADVANCE);
  mfkey_slice_t odd = {.tail = -1}, even = {.tail = -1};
  for (int32_t bit = MK_WORD_BITS - 1; bit >= 0; bit -= 2)
    odd.stream = (odd.stream << 1) | MK_BEBIT(stream, bit);
  for (int32_t bit = MK_WORD_BITS - 2; bit >= 0; bit -= 2)
    even.stream = (even.stream << 1) | MK_BEBIT(stream, bit);
  for (int32_t state = 1U << MK_FILTER_BITS; state >= 0; state--) {
    if (!checkpoint(&search))
      goto cleanup;
    uint32_t output = filter((uint32_t)state);
    if (output == (odd.stream & 1U))
      search.odd[++odd.tail] = (uint32_t)state;
    if (output == (even.stream & 1U))
      search.even[++even.tail] = (uint32_t)state;
  }
  for (int32_t round = 0; round < MK_SIMPLE_ROUNDS; round++) {
    odd.stream >>= 1;
    even.stream >>= 1;
    if (!extend(&search, &odd, true, false) || !extend(&search, &even, false, false))
      goto cleanup;
  }
  recover(&search, odd, even, MK_REMAINING_ROUNDS);
cleanup:
  free(search.odd);
  free(search.even);
  if (search.result == MFKEY_RESULT_FOUND)
    *out_key = search.key;
  return search.result;
}

bool mfkey32(uint32_t uid,
             uint32_t nt0,
             uint32_t nr0,
             uint32_t ar0,
             uint32_t nt1,
             uint32_t nr1,
             uint32_t ar1,
             uint64_t *key) {
  const mfkey_pair_t pair = {uid, nt0, nr0, ar0, nt1, nr1, ar1};
  return mfkey_recover(&pair, key, NULL, NULL) == MFKEY_RESULT_FOUND;
}
