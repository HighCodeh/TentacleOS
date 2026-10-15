/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stddef.h>
#include "mdns_private.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Lock mdns service
 */
void mdns_priv_service_lock(void);

/**
 * @brief  Unlock mdns service
 */
void mdns_priv_service_unlock(void);

/**
 * @brief  Send the given action to the service queue
 */
bool mdns_priv_queue_action(mdns_action_t *action);

#ifdef __cplusplus
}
#endif
