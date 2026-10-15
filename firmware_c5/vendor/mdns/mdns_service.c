/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "sdkconfig.h"
#include "mdns_private.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_check.h"
#include "mdns.h"
#include "mdns_mem_caps.h"
#include "mdns_utils.h"
#include "mdns_browser.h"
#include "mdns_netif.h"
#include "mdns_send.h"
#include "mdns_receive.h"
#include "mdns_querier.h"
#include "mdns_pcb.h"
#include "mdns_responder.h"

#define MDNS_SERVICE_STACK_DEPTH    CONFIG_MDNS_TASK_STACK_SIZE
#define MDNS_TASK_PRIORITY          CONFIG_MDNS_TASK_PRIORITY
#if (MDNS_TASK_PRIORITY > ESP_TASK_PRIO_MAX)
#error "mDNS task priority is higher than ESP_TASK_PRIO_MAX"
#elif (MDNS_TASK_PRIORITY > ESP_TASKD_EVENT_PRIO)
#warning "mDNS task priority is higher than ESP_TASKD_EVENT_PRIO, mDNS library might not work correctly"
#endif
#define MDNS_TASK_AFFINITY          CONFIG_MDNS_TASK_AFFINITY

#define MDNS_SERVICE_LOCK()     xSemaphoreTake(s_service_semaphore, portMAX_DELAY)
#define MDNS_SERVICE_UNLOCK()   xSemaphoreGive(s_service_semaphore)

static volatile TaskHandle_t s_service_task_handle = NULL;
static SemaphoreHandle_t s_service_semaphore = NULL;
static StackType_t *s_stack_buffer;
static QueueHandle_t s_action_queue;
static esp_timer_handle_t s_timer_handle;

static const char *TAG = "mdns_service";

#ifdef CONFIG_MDNS_RESPOND_REVERSE_QUERIES
static inline char nibble_to_hex(int var)
{
    return var > 9 ?  var - 10 + 'a' : var + '0';
}
#endif

/**
 * @brief  Performs interface changes based on system events or custom commands
 */
static void perform_event_action(mdns_if_t mdns_if, mdns_event_actions_t action)
{
    if (!mdns_priv_is_server_init() || mdns_if >= MDNS_MAX_INTERFACES) {
        return;
    }
    if (action & MDNS_EVENT_ENABLE_IP4) {
        mdns_priv_pcb_enable(mdns_if, MDNS_IP_PROTOCOL_V4);
    }
    if (action & MDNS_EVENT_ENABLE_IP6) {
        mdns_priv_pcb_enable(mdns_if, MDNS_IP_PROTOCOL_V6);
    }
    if (action & MDNS_EVENT_DISABLE_IP4) {
        mdns_priv_pcb_disable(mdns_if, MDNS_IP_PROTOCOL_V4);
    }
    if (action & MDNS_EVENT_DISABLE_IP6) {
        mdns_priv_pcb_disable(mdns_if, MDNS_IP_PROTOCOL_V6);
    }
    if (action & MDNS_EVENT_ANNOUNCE_IP4) {
        mdns_priv_pcb_announce(mdns_if, MDNS_IP_PROTOCOL_V4, NULL, 0, true);
    }
    if (action & MDNS_EVENT_ANNOUNCE_IP6) {
        mdns_priv_pcb_announce(mdns_if, MDNS_IP_PROTOCOL_V6, NULL, 0, true);
    }

#ifdef CONFIG_MDNS_RESPOND_REVERSE_QUERIES
#ifdef CONFIG_LWIP_IPV4
    if (action & MDNS_EVENT_IP4_REVERSE_LOOKUP) {
        esp_netif_ip_info_t if_ip_info;
        if (esp_netif_get_ip_info(mdns_priv_get_esp_netif(mdns_if), &if_ip_info) == ESP_OK) {
            esp_ip4_addr_t *ip = &if_ip_info.ip;
            char *reverse_query_name = NULL;
            if (asprintf(&reverse_query_name, "%d.%d.%d.%d.in-addr",
                         esp_ip4_addr4_16(ip), esp_ip4_addr3_16(ip),
                         esp_ip4_addr2_16(ip), esp_ip4_addr1_16(ip)) > 0 && reverse_query_name) {
                ESP_LOGD(TAG, "Registered reverse query: %s.arpa", reverse_query_name);
                mdns_priv_delegate_hostname_add(reverse_query_name, NULL);
            }
        }
    }
#endif /* CONFIG_LWIP_IPV4 */
#ifdef CONFIG_LWIP_IPV6
    if (action & MDNS_EVENT_IP6_REVERSE_LOOKUP) {
        esp_ip6_addr_t addr6;
        if (!esp_netif_get_ip6_linklocal(mdns_priv_get_esp_netif(mdns_if), &addr6) && !mdns_utils_ipv6_address_is_zero(addr6)) {
            uint8_t *paddr = (uint8_t *)&addr6.addr;
            const char sub[] = "ip6";
            const size_t query_name_size = 4 * sizeof(addr6.addr) /* (2 nibbles + 2 dots)/per byte of IP address */ + sizeof(sub);
            char *reverse_query_name = mdns_mem_malloc(query_name_size);
            if (reverse_query_name) {
                char *ptr = &reverse_query_name[query_name_size];   // point to the end
                memcpy(ptr - sizeof(sub), sub, sizeof(sub));        // copy the IP sub-domain
                ptr -= sizeof(sub) + 1;                             // move before the sub-domain
                while (reverse_query_name < ptr) {                  // continue populating reverse query from the end
                    *ptr-- = '.';                                   // nibble by nibble, until we reach the beginning
                    *ptr-- = nibble_to_hex(((*paddr) >> 4) & 0x0F);
                    *ptr-- = '.';
                    *ptr-- = nibble_to_hex((*paddr) & 0x0F);
                    paddr++;
                }
                ESP_LOGD(TAG, "Registered reverse query: %s.arpa", reverse_query_name);
                mdns_priv_delegate_hostname_add(reverse_query_name, NULL);
            }
        }
    }
#endif /* CONFIG_LWIP_IPV6 */
#endif /* CONFIG_MDNS_RESPOND_REVERSE_QUERIES */
}

/**
 * @brief  Free action data
 */
static void free_action(mdns_action_t *action)
{
    switch (action->type) {
    case ACTION_SEARCH_ADD:
    case ACTION_SEARCH_SEND:
    case ACTION_SEARCH_END:
        mdns_priv_query_action(action, ACTION_CLEANUP);
        break;
    case ACTION_BROWSE_ADD:
    case ACTION_BROWSE_END:
    case ACTION_BROWSE_SYNC:
        mdns_priv_browse_action(action, ACTION_CLEANUP);
        break;
    case ACTION_TX_HANDLE:
        mdns_priv_send_action(action, ACTION_CLEANUP);
        break;
    case ACTION_RX_HANDLE:
        mdns_priv_receive_action(action, ACTION_CLEANUP);
        break;
    case ACTION_HOSTNAME_SET:
    case ACTION_INSTANCE_SET:
    case ACTION_DELEGATE_HOSTNAME_SET_ADDR:
    case ACTION_DELEGATE_HOSTNAME_REMOVE:
    case ACTION_DELEGATE_HOSTNAME_ADD:
        mdns_priv_responder_action(action, ACTION_CLEANUP);
        break;
    default:
        break;
    }
    mdns_mem_free(action);
}

/**
 * @brief  Called from service thread to execute given action
 */
static void execute_action(mdns_action_t *action)
{
    switch (action->type) {
    case ACTION_SYSTEM_EVENT:
        perform_event_action(action->data.sys_event.interface, action->data.sys_event.event_action);
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

/**
 * @brief  the main MDNS service task. Packets are received and parsed here
 */
static void service_task(void *pvParameters)
{
    mdns_action_t *a = NULL;
    for (;;) {
        if (mdns_priv_is_server_init() && s_action_queue) {
            if (xQueueReceive(s_action_queue, &a, portMAX_DELAY) == pdTRUE) {
                assert(a);
                if (a->type == ACTION_TASK_STOP) {
                    break;
                }
                MDNS_SERVICE_LOCK();
                execute_action(a);
                MDNS_SERVICE_UNLOCK();
            }
        } else {
            vTaskDelay(500 * portTICK_PERIOD_MS);
        }
    }
    s_service_task_handle = NULL;
    vTaskDelay(portMAX_DELAY);
}

static void timer_cb(void *arg)
{
    mdns_priv_send_packets();
    mdns_priv_query_start_stop();
}

static esp_err_t start_timer(void)
{
    esp_timer_create_args_t timer_conf = {
        .callback = timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "mdns_timer"
    };
    esp_err_t err = esp_timer_create(&timer_conf, &(s_timer_handle));
    if (err) {
        return err;
    }
    return esp_timer_start_periodic(s_timer_handle, MDNS_TIMER_PERIOD_US);
}

static esp_err_t stop_timer(void)
{
    esp_err_t err = ESP_OK;
    if (s_timer_handle) {
        err = esp_timer_stop(s_timer_handle);
        if (err) {
            return err;
        }
        err = esp_timer_delete(s_timer_handle);
    }
    return err;
}

static esp_err_t create_task_with_caps(void)
{
    esp_err_t ret = ESP_OK;
    static StaticTask_t mdns_task_buffer;

    s_stack_buffer = mdns_mem_task_malloc(MDNS_SERVICE_STACK_DEPTH);
    ESP_GOTO_ON_FALSE(s_stack_buffer != NULL, ESP_FAIL, alloc_failed, TAG, "failed to allocate memory for the mDNS task's stack");

    s_service_task_handle = xTaskCreateStaticPinnedToCore(service_task, "mdns", MDNS_SERVICE_STACK_DEPTH, NULL, MDNS_TASK_PRIORITY, s_stack_buffer, &mdns_task_buffer, MDNS_TASK_AFFINITY);
    ESP_GOTO_ON_FALSE(s_service_task_handle != NULL, ESP_FAIL, err, TAG, "failed to create task for the mDNS");

    return ret;

alloc_failed:
    HOOK_MALLOC_FAILED;
err:
    mdns_mem_task_free(s_stack_buffer);
    return ret;
}

/**
 * @brief  Start the service thread if not running
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t service_task_start(void)
{
    esp_err_t ret = ESP_OK;
    if (!s_service_semaphore) {
        s_service_semaphore = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_service_semaphore != NULL, ESP_FAIL, TAG, "Failed to create the mDNS service lock");
    }
    MDNS_SERVICE_LOCK();
    ESP_GOTO_ON_ERROR(start_timer(), err, TAG, "Failed to start the mDNS service timer");

    if (!s_service_task_handle) {
        ESP_GOTO_ON_ERROR(create_task_with_caps(), err_stop_timer, TAG, "Failed to start the mDNS service task");
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0) && !CONFIG_IDF_TARGET_LINUX
        StackType_t *mdns_debug_stack_buffer;
        StaticTask_t *mdns_debug_task_buffer;
        xTaskGetStaticBuffers(s_service_task_handle, &mdns_debug_stack_buffer, &mdns_debug_task_buffer);
        ESP_LOGD(TAG, "mdns_debug_stack_buffer:%p mdns_debug_task_buffer:%p\n", mdns_debug_stack_buffer, mdns_debug_task_buffer);
#endif // CONFIG_IDF_TARGET_LINUX
    }
    MDNS_SERVICE_UNLOCK();
    return ret;

err_stop_timer:
    stop_timer();
err:
    MDNS_SERVICE_UNLOCK();
    vSemaphoreDelete(s_service_semaphore);
    s_service_semaphore = NULL;
    return ret;
}

/**
 * @brief  Stop the service thread
 *
 * @return
 *      - ESP_OK
 */
static esp_err_t service_task_stop(void)
{
    stop_timer();
    if (s_service_task_handle) {
        TaskHandle_t task_handle = s_service_task_handle;
        mdns_action_t action;
        mdns_action_t *a = &action;
        action.type = ACTION_TASK_STOP;
        if (xQueueSend(s_action_queue, &a, (TickType_t)0) != pdPASS) {
            s_service_task_handle = NULL;
        }
        while (s_service_task_handle) {
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }
        vTaskDelete(task_handle);
    }
    vSemaphoreDelete(s_service_semaphore);
    s_service_semaphore = NULL;
    return ESP_OK;
}

void mdns_priv_service_lock(void)
{
    MDNS_SERVICE_LOCK();
}

void mdns_priv_service_unlock(void)
{
    MDNS_SERVICE_UNLOCK();
}

esp_err_t mdns_init(void)
{
    esp_err_t err = ESP_OK;

    if (mdns_priv_is_server_init()) {
        return err;
    }

    if (mdns_priv_responder_init() != ESP_OK) {
        return ESP_ERR_NO_MEM;
    }

    s_action_queue = xQueueCreate(MDNS_ACTION_QUEUE_LEN, sizeof(mdns_action_t *));
    if (!s_action_queue) {
        err = ESP_ERR_NO_MEM;
        goto free_responder;
    }

    if (mdns_priv_netif_init() != ESP_OK) {
        err = ESP_FAIL;
        goto free_queue;
    }

    if (service_task_start()) {
        //service start failed!
        err = ESP_FAIL;
        goto free_all_and_disable_pcbs;
    }

    return ESP_OK;

free_all_and_disable_pcbs:
    mdns_priv_netif_deinit();
free_queue:
    vQueueDelete(s_action_queue);
free_responder:
    mdns_priv_responder_free();
    return err;
}

void mdns_free(void)
{
    if (!mdns_priv_is_server_init()) {
        return;
    }

    // Unregister handlers before destroying the mdns internals to avoid receiving async events while deinit
    mdns_priv_netif_unregister_predefined_handlers();

    mdns_service_remove_all();
    service_task_stop();
    // at this point, the service task is deleted, so we can destroy the stack size
    mdns_mem_task_free(s_stack_buffer);
    mdns_priv_pcb_deinit();
    if (s_action_queue) {
        mdns_action_t *c;
        while (xQueueReceive(s_action_queue, &c, 0) == pdTRUE) {
            free_action(c);
        }
        vQueueDelete(s_action_queue);
    }
    mdns_priv_clear_tx_queue();
    mdns_priv_query_free();
    mdns_priv_browse_free();
    mdns_priv_responder_free();
}

bool mdns_priv_queue_action(mdns_action_t *action)
{
    if (xQueueSend(s_action_queue, &action, (TickType_t)0) != pdPASS) {
        return false;
    }
    return true;
}
