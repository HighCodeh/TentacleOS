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

#ifndef SUBGHZ_TRANSMITTER_H
#define SUBGHZ_TRANSMITTER_H

#include <stdint.h>
#include <stddef.h>

void subghz_tx_init(void);
void subghz_tx_stop(void);
void subghz_tx_send_raw(const int32_t *timings, size_t count);

#endif // SUBGHZ_TRANSMITTER_H
