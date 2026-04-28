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

#ifndef MT_MOD_KEYVERIFY_H
#define MT_MOD_KEYVERIFY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "mt_modules.h"

/**
 * @brief Initialize the Key Verification module.
 */
void mt_mod_keyverify_init(uint32_t node_num);

/**
 * @brief Handle an incoming KeyVerification proto on PortNum 12.
 *
 * Parses nonce and hash1/hash2 fields. If hash1 matches the expected
 * verification for the tracked peer, marks the NodeDB entry as
 * key_manually_verified.
 */
void mt_mod_keyverify_on_received(const mt_packet_meta_t *meta,
                                   const uint8_t *payload, uint16_t len);

/**
 * @brief Handle an AdminMessage.key_verification (variant 67) from the
 * connected phone.
 *
 * Drives the 4-stage FSM:
 *  - INITIATE_VERIFICATION: send hash2 to remote with a fresh nonce
 *  - PROVIDE_SECURITY_NUMBER: user entered code, send hash1 to remote
 *  - DO_VERIFY: mark current remote as verified
 *  - DO_NOT_VERIFY: reset state
 *
 * @param remote_nodenum   Target node (field 2 of KeyVerificationAdmin)
 * @param message_type     Stage (0..3)
 * @param nonce            FSM nonce (field 3)
 * @param has_security     Whether security_number is present
 * @param security_number  4-digit code (field 4)
 */
void mt_mod_keyverify_handle_admin(uint32_t remote_nodenum,
                                    uint8_t message_type,
                                    uint64_t nonce,
                                    bool has_security,
                                    uint32_t security_number);

#ifdef __cplusplus
}
#endif

#endif // MT_MOD_KEYVERIFY_H
