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

#ifndef STORAGE_STREAM_H
#define STORAGE_STREAM_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef struct storage_stream *storage_stream_t;

storage_stream_t storage_stream_open(const char *path, const char *mode);
esp_err_t        storage_stream_write(storage_stream_t stream, const void *data, size_t size);
esp_err_t        storage_stream_read(storage_stream_t stream, void *buf, size_t size, size_t *bytes_read);
esp_err_t        storage_stream_flush(storage_stream_t stream);
esp_err_t        storage_stream_close(storage_stream_t stream);
bool             storage_stream_is_open(storage_stream_t stream);
size_t           storage_stream_bytes_written(storage_stream_t stream);

#endif // STORAGE_STREAM_H
