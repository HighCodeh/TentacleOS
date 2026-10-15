/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * IMA ADPCM, one 4-bit code per 16-bit sample. Lets boards without PSRAM keep
 * the pre-roll in a quarter of the RAM. Encoder and decoder states start
 * zeroed and must see the same samples in the same order.
 */

typedef struct {
    int16_t pred;
    int8_t index;
} muse_adpcm_t;

/* n samples (even) to or from n/2 bytes, the first sample of each pair in the low nibble. */
void muse_adpcm_encode_block(muse_adpcm_t *s, const int16_t *pcm, size_t n, uint8_t *out);
void muse_adpcm_decode_block(muse_adpcm_t *s, const uint8_t *in, size_t n, int16_t *pcm);
