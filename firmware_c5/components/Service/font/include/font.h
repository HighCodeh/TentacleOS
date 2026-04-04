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


#pragma once
#include <stdint.h>

typedef struct {
    const uint8_t *bitmap;     // Dados dos glifos (bitmap contíguo)
    uint16_t first_char;       // Primeiro caractere
    uint16_t last_char;        // Último caractere
    uint8_t width;             // Largura do glifo
    uint8_t height;            // Altura do glifo
    uint8_t spacing;           // Espaçamento entre caracteres
} font_t;

// Fontes disponíveis
extern const font_t font_ubuntu_16;
extern const font_t font_terminus_24;
extern const font_t font_arial_32;
