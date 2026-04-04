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


#ifndef GATT_EXPLORER_H
#define GATT_EXPLORER_H

#include <stdbool.h>
#include <stdint.h>

bool gatt_explorer_start(const uint8_t *addr, uint8_t addr_type);
bool gatt_explorer_is_busy(void);

#endif // GATT_EXPLORER_H
