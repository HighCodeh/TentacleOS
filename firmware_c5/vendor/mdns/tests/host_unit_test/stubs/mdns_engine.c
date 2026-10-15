/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "mdns.h"
#include "mdns_private.h"
#include "mdns_querier.h"
#include "mdns_browser.h"
#include "mdns_send.h"
#include "mdns_responder.h"
#include "mdns_receive.h"
#include "mdns_mem_caps.h"

static void execute_action(mdns_action_t *action)
{
    switch (action->type) {
    case ACTION_SYSTEM_EVENT:
        break;
    case ACTION_SEARCH_ADD:
    case ACTION_SEARCH_SEND:
    case ACTION_SEARCH_END:
        mdns_priv_query_action(action, ACTION_RUN);
        break;
    case ACTION_BROWSE_ADD:
    case ACTION_BROWSE_SYNC:
    case ACTION_BROWSE_END:
        mdns_priv_browse_action(action, ACTION_RUN);
        break;

    case ACTION_TX_HANDLE:
        mdns_priv_send_action(action, ACTION_RUN);
        break;
    case ACTION_RX_HANDLE:
        mdns_priv_receive_action(action, ACTION_RUN);
        break;
    case ACTION_HOSTNAME_SET:
    case ACTION_INSTANCE_SET:
    case ACTION_DELEGATE_HOSTNAME_ADD:
    case ACTION_DELEGATE_HOSTNAME_SET_ADDR:
    case ACTION_DELEGATE_HOSTNAME_REMOVE:
        mdns_priv_responder_action(action, ACTION_RUN);
        break;
    default:
        break;
    }
    mdns_mem_free(action);
}

bool mdns_priv_queue_action(mdns_action_t *action)
{
    execute_action(action);
    return true;
}

void mdns_priv_service_lock(void)
{
}

void mdns_priv_service_unlock(void)
{
}
