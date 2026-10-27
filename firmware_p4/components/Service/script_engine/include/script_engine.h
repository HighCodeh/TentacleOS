// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#ifndef SCRIPT_ENGINE_H
#define SCRIPT_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SCRIPT_ENGINE_SOURCE_MAX (64U * 1024U)
#define SCRIPT_ENGINE_HEAP_MAX   (512U * 1024U)
#define SCRIPT_ENGINE_ERROR_MAX  192

typedef enum {
  SCRIPT_ENGINE_JS = 0,
  SCRIPT_ENGINE_LUA,
  SCRIPT_ENGINE_LANGUAGE_COUNT
} script_engine_language_t;

typedef enum {
  SCRIPT_ENGINE_OK = 0,
  SCRIPT_ENGINE_ERROR,
  SCRIPT_ENGINE_STOPPED,
  SCRIPT_ENGINE_NO_MEMORY,
  SCRIPT_ENGINE_INVALID_ARG,
  SCRIPT_ENGINE_RESULT_COUNT
} script_engine_result_t;

typedef void (*script_engine_output_cb_t)(void *ctx, const char *text, size_t length);
typedef bool (*script_engine_poll_cb_t)(void *ctx);
typedef void (*script_engine_sleep_cb_t)(void *ctx, uint32_t milliseconds);
typedef int64_t (*script_engine_clock_cb_t)(void *ctx);
// Allocations must be compatible with free().
typedef void *(*script_engine_alloc_cb_t)(size_t length);

struct script_bindings;

typedef struct {
  struct script_bindings *bindings;
  void *ctx;
  script_engine_output_cb_t output;
  script_engine_poll_cb_t poll;
  script_engine_sleep_cb_t sleep;
  script_engine_clock_cb_t clock;
  script_engine_alloc_cb_t alloc;
} script_engine_host_t;

typedef struct {
  script_engine_language_t language;
  const char *source;
  size_t length;
  const char *name;
  const script_engine_host_t *host;
  char error[SCRIPT_ENGINE_ERROR_MAX];
} script_engine_request_t;

// Source must have a NUL after length; caller owns source and callbacks until return.
script_engine_result_t script_engine_run(script_engine_request_t *request);

#ifdef __cplusplus
}
#endif
#endif
