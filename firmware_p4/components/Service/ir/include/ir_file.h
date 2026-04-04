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

#ifndef IR_FILE_H
#define IR_FILE_H

#include "ir.h"

#define IR_FILE_NAME_MAX 32

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[IR_FILE_NAME_MAX];
    bool is_raw;
    ir_data_t data;
    uint32_t  frequency;
    uint32_t *raw;          // malloc'd: alternating mark/space durations in us
    size_t    raw_count;
} ir_signal_t;

typedef struct {
    ir_signal_t *signals;   // malloc'd array
    size_t count;
    size_t capacity;
} ir_file_t;

void ir_file_init(ir_file_t *file);
void ir_file_free(ir_file_t *file);

bool ir_file_parse(const char *content, ir_file_t *file);
size_t ir_file_to_string(const ir_file_t *file, char *buf, size_t buf_size);
ir_signal_t *ir_file_find(const ir_file_t *file, const char *name);
void ir_file_send(const ir_signal_t *signal);
void ir_file_add_parsed(ir_file_t *file, const char *name, const ir_data_t *data);
void ir_file_add_raw(ir_file_t *file, const char *name,
                     const rmt_symbol_word_t *symbols, size_t count, uint32_t freq);

#ifdef __cplusplus
}
#endif

#endif // IR_FILE_H
