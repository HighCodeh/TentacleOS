// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef HOST_LINK_LOCAL_IR_H
#define HOST_LINK_LOCAL_IR_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "host_link_rf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HOST_LOCAL_IR_CODE_SIZE      10
#define HOST_LOCAL_IR_TIMEOUT_MAX_MS 1000
bool host_local_ir_is_command(uint16_t command);
/* Persistent host worker only, behind the local-call kill gate. */
esp_err_t host_local_ir_execute(host_rf_request_t *request);

#ifdef __cplusplus
}
#endif
#endif // HOST_LINK_LOCAL_IR_H
