// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef HOST_LINK_LOCAL_RF_H
#define HOST_LINK_LOCAL_RF_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "host_link_rf.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t host_local_rf_init(void);
bool host_local_rf_is_ready(void);
bool host_local_rf_lock(uint32_t timeout_ms);
void host_local_rf_unlock(void);
/** Caller holds the local RF lock until this synchronous call returns. */
esp_err_t host_local_rf_execute(host_rf_request_t *request);
/** Host core worker only; queued local work never emits a transport response. */
void host_local_rf_process(void);
/** Queue a trusted local RF operation; used only by the internal broker. */
esp_err_t host_link_schedule_local_rf(void);

#ifdef __cplusplus
}
#endif
#endif // HOST_LINK_LOCAL_RF_H
