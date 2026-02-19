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

#ifndef SUBGHZ_RECEIVER_H
#define SUBGHZ_RECEIVER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    int32_t *items;
    size_t count;
} subghz_raw_signal_t;

typedef enum {
    SUBGHZ_MODE_SCAN, 
    SUBGHZ_MODE_RAW   
} subghz_mode_t;

typedef enum {
    SUBGHZ_MODULATION_ASK, // Amplitude Shift Keying (OOK) - Padrão
    SUBGHZ_MODULATION_FSK  // Frequency Shift Keying (Intelbras, etc)
} subghz_modulation_t;

void subghz_receiver_start(subghz_mode_t mode, subghz_modulation_t mod, uint32_t freq);
void subghz_receiver_stop(void);
bool subghz_receiver_is_running(void);

#endif // SUBGHZ_RECEIVER_H
