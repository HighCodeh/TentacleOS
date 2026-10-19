// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef IR_LEASE_H
#define IR_LEASE_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal persistent-worker lease. Native IR calls are rejected while held. */
esp_err_t ir_lease_acquire(void);
esp_err_t ir_lease_release(void);
bool ir_lease_enter(void);
void ir_lease_leave(bool is_active);

#ifdef __cplusplus
}
#endif
#endif // IR_LEASE_H
