// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SCRIPT_RUNNER_H
#define SCRIPT_RUNNER_H

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "script_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCRIPT_RUNNER_DIRECTORY  "/sdcard/scripts"
#define SCRIPT_RUNNER_NAME_MAX   64
#define SCRIPT_RUNNER_OUTPUT_MAX 4096
#define SCRIPT_RUNNER_TIMEOUT_MS 30000

typedef struct {
  bool is_running;
  script_engine_result_t result;
  char name[SCRIPT_RUNNER_NAME_MAX];
  char output[SCRIPT_RUNNER_OUTPUT_MAX];
  char error[SCRIPT_ENGINE_ERROR_MAX];
} script_runner_snapshot_t;

bool script_runner_name_is_valid(const char *name);
esp_err_t script_runner_start(const char *name);
void script_runner_stop(void);
void script_runner_snapshot(script_runner_snapshot_t *out_snapshot);

#ifdef __cplusplus
}
#endif
#endif
