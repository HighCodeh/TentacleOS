// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SCRIPT_BINDINGS_H
#define SCRIPT_BINDINGS_H

#include <stdbool.h>
#include <stdint.h>

#include "tos_api.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCRIPT_BINDINGS_ARGS_MAX   7
#define SCRIPT_BINDINGS_STRING_MAX 257
#define SCRIPT_BINDINGS_FILES_MAX  4
#define SCRIPT_BINDINGS_READ_MAX   128
#define SCRIPT_BINDINGS_LIST_MAX   8
#define SCRIPT_BINDINGS_CAPS \
  (TOS_CAP_FS_READ | TOS_CAP_RADIO_TX | TOS_CAP_RADIO_RX | TOS_CAP_HID | TOS_CAP_UI)

typedef struct {
  char type;
  double number;
  char text[SCRIPT_BINDINGS_STRING_MAX];
} script_bindings_value_t;

typedef struct {
  const char *method;
  uint32_t count;
  script_bindings_value_t args[SCRIPT_BINDINGS_ARGS_MAX];
  cJSON *result;
} script_bindings_call_t;

typedef void (*script_bindings_display_cb_t)(bool is_open);

typedef struct script_bindings {
  const tos_api_t *api;
  uint32_t caps;
  struct {
    void *file;
    uint32_t token;
  } files[SCRIPT_BINDINGS_FILES_MAX];
  uint32_t next_token;
  script_bindings_display_cb_t display;
} script_bindings_t;

// The caller owns args and result; delete result even on an error.
esp_err_t script_bindings_call(script_bindings_t *bindings, script_bindings_call_t *call);
void script_bindings_close(script_bindings_t *bindings);
esp_err_t script_bindings_requested_caps(const char *source, uint32_t *out_caps);

#ifdef __cplusplus
}
#endif
#endif
