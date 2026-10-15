/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"

void esp_log(esp_log_config_t config, const char* tag, const char* format, ...)
{}

uint32_t esp_log_timestamp(void)
{
    return 0;
}

uint32_t esp_get_free_heap_size(void)
{
    return 0;
}

esp_netif_t *esp_netif_get_handle_from_ifkey(const char *if_key)
{
    return (esp_netif_t*)1;
}

esp_err_t esp_netif_get_ip_info(esp_netif_t *esp_netif, esp_netif_ip_info_t *ip_info)
{
    ip_info->ip.addr = 0xC0A80164;      // 192.168.1.100
    ip_info->netmask.addr = 0xFFFFFF00; // 255.255.255.0
    ip_info->gw.addr = 0xC0A80101;      // 192.168.1.1
    return ESP_OK;
}

esp_err_t esp_netif_get_ip6_linklocal(esp_netif_t *esp_netif, esp_ip6_addr_t *if_ip6)
{
    if_ip6->addr[0] = 0xFE80;
    if_ip6->addr[1] = 0x0000;
    if_ip6->addr[2] = 0x0000;
    if_ip6->addr[3] = 0x0001;
    return ESP_OK;
}

uint32_t esp_random(void)
{
    return 0;
}

TickType_t xTaskGetTickCount(void)
{
    return 0;
}

int esp_netif_get_all_ip6(esp_netif_t *esp_netif, esp_ip6_addr_t if_ip6[])
{
    if_ip6[0].addr[0] = 0xFE80;
    if_ip6[0].addr[1] = 0x0000;
    if_ip6[0].addr[2] = 0x0000;
    if_ip6[0].addr[3] = 0x0001;
    return 1;
}


BaseType_t xSemaphoreGive(QueueHandle_t xQueue)
{
    return pdPASS;
}

BaseType_t xSemaphoreTake(QueueHandle_t xQueue, TickType_t xTicksToWait)
{
    return pdPASS;
}

QueueHandle_t xSemaphoreCreateBinary(void)
{
    return (QueueHandle_t)1;
}

void vQueueDelete(QueueHandle_t xQueue)
{
}
