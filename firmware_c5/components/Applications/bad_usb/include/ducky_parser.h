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

#ifndef DUCKY_PARSER_H
#define DUCKY_PARSER_H

#include <esp_err.h>
#include <stdbool.h>
typedef void (*ducky_progress_cb_t)(int current_line, int total_lines);

typedef enum {
    DUCKY_LAYOUT_US = 0,
    DUCKY_LAYOUT_ABNT2 = 1
} ducky_layout_t;

typedef enum {
    DUCKY_OUTPUT_USB = 0,
    DUCKY_OUTPUT_BLUETOOTH = 1
} ducky_output_mode_t;
void ducky_set_output_mode(ducky_output_mode_t mode);
void ducky_parse_and_run(const char *script);
void ducky_set_progress_callback(ducky_progress_cb_t cb);
void ducky_set_layout(ducky_layout_t layout);
esp_err_t ducky_run_from_assets(const char *filename);
esp_err_t ducky_run_from_sdcard(const char *path);
void ducky_abort(void);

#endif // DUCKY_PARSER_H
