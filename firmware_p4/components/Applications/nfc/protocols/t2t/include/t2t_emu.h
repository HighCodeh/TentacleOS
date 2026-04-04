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
/**
 * @file t2t_emu.h
 * @brief NFC-A Type 2 Tag (T2T) Emulation ST25R3916
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "highboy_nfc_error.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    T2T_TAG_NTAG213 = 0,
    T2T_TAG_NTAG215,
    T2T_TAG_NTAG216,
    T2T_TAG_ULTRALIGHT_C,
} t2t_tag_type_t;

typedef struct {
    t2t_tag_type_t type;
    const char*    ndef_text;

    uint8_t        uid[7];
    uint8_t        uid_len;

    bool           enable_pwd;
    uint8_t        pwd[4];
    uint8_t        pack[2];
    uint8_t        auth0;
    uint8_t        access;

    bool           enable_ulc_auth;
    uint8_t        ulc_key[16];
} t2t_emu_config_t;

hb_nfc_err_t t2t_emu_init(const t2t_emu_config_t* cfg);
hb_nfc_err_t t2t_emu_init_default(void);
hb_nfc_err_t t2t_emu_configure_target(void);
hb_nfc_err_t t2t_emu_start(void);
void         t2t_emu_stop(void);
uint8_t*     t2t_emu_get_mem(void);
void         t2t_emu_run_step(void);

#ifdef __cplusplus
}
#endif
