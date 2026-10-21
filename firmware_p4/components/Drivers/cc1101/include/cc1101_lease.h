// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CC1101_LEASE_H
#define CC1101_LEASE_H

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Persistent worker only. Release after RMT, buffers and radio are quiescent. */
esp_err_t cc1101_lease_acquire(void);
esp_err_t cc1101_lease_release(void);
/** Retain failed cleanup but reject unrelated commands on the same worker. */
void cc1101_lease_pause(void);
bool cc1101_lease_enter(void);
void cc1101_lease_leave(void);
void cc1101_lease_record_error(esp_err_t error);
esp_err_t cc1101_lease_take_error(void);

#ifdef __cplusplus
}
#endif
#endif // CC1101_LEASE_H
