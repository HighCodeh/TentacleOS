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


#ifndef TUSB_DESC_H
#define TUSB_DESC_H


#include "tinyusb.h"
// --- Constantes Públicas ---
// Define um nome para a nossa única interface HID (Teclado)
#define ITF_NUM_HID   0

// --- Declarações Externas ---
// Informa a outros arquivos que essas variáveis existem em algum lugar (no tusb_desc.c)
void busb_init(void);

#endif // TUSB_DESC_H
