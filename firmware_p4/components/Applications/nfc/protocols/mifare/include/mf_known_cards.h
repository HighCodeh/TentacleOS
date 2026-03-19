// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#ifndef MF_KNOWN_CARDS_H
#define MF_KNOWN_CARDS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Known MIFARE Classic card type database.
 *
 * Matches a card by SAK + ATQA (and optionally UID prefix).
 * When a match is found, provides:
 *   - Human-readable name and hint string
 *   - Priority key list to try BEFORE the full dictionary
 *     (these keys are card-type-specific and will hit much faster)
 */

typedef struct {
    const char*    name;
    const char*    hint;
    uint8_t        sak;
    uint8_t        atqa[2];
    uint8_t        uid_prefix[4];
    uint8_t        uid_prefix_len;   /* 0 = do not check UID prefix */
    const uint8_t (*hint_keys)[6];  /* priority keys, may be NULL */
    int            hint_key_count;
} mf_known_card_t;

/*
 * Try to match a card against the database.
 * Returns a pointer to the matched entry, or NULL if unknown.
 */
const mf_known_card_t* mf_known_cards_match(uint8_t        sak,
                                              const uint8_t  atqa[2],
                                              const uint8_t* uid,
                                              uint8_t        uid_len);

#ifdef __cplusplus
}
#endif
#endif /* MF_KNOWN_CARDS_H */
