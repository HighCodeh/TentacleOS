// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include "ethernet_router.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "grok_wifi.h"
#include "session_manager.h"
#include "spi_bridge.h"
#include "sys_prio.h"
#include "wifi_service.h"

#define PACKET_DEPTH          8
#define ROUTER_STACK          6144
#define ROUTER_POLL_MS        10
#define MASTER_TIMEOUT_US     (5000LL * 1000)
#define DHCP_TIMEOUT_US       (60000LL * 1000)
#define ETHERNET_HEADER_SIZE  14
#define UPLINK_ROUTE_PRIORITY 150
#define DHCP_OFFER_DNS        2

static const char *TAG = "ETH_ROUTER";
typedef struct {
  uint16_t length;
  uint8_t bytes[SPI_ETHERNET_MTU];
} ethernet_router_packet_t;
static atomic_bool s_active, s_stop;
static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_storage;
static QueueHandle_t s_rx, s_tx;
static StaticQueue_t s_rx_control, s_tx_control;
static uint8_t *s_storage;
static ethernet_router_packet_t *s_rx_packet, *s_tx_packet;
static uint16_t s_rx_offset, s_rx_seq, s_tx_offset, s_tx_seq;
static spi_ethernet_start_t s_start;
static spi_ethernet_status_t s_status;
static int64_t s_heartbeat;
static esp_netif_t *s_netif;

bool ethernet_router_is_active(void) {
  return atomic_load(&s_active);
}
static void free_rx(void *handle, void *buffer) {
  (void)handle;
  free(buffer);
}
static esp_err_t transmit(void *handle, void *buffer, size_t length) {
  (void)handle;
  if (!atomic_load(&s_active) || atomic_load(&s_stop) || length > SPI_ETHERNET_MTU)
    return ESP_ERR_INVALID_STATE;
  ethernet_router_packet_t *packet = heap_caps_malloc(sizeof(*packet), MALLOC_CAP_SPIRAM);
  if (packet == NULL) {
    ESP_LOGE(TAG, "TX allocation failed");
    return ESP_ERR_NO_MEM;
  }
  packet->length = length;
  memcpy(packet->bytes, buffer, length);
  bool queued = xQueueSend(s_tx, packet, 0) == pdTRUE;
  free(packet);
  return queued ? ESP_OK : ESP_ERR_NO_MEM;
}
static void failure(esp_err_t error) {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_status.error = error;
  s_status.state = SPI_ETHERNET_STATE_ERROR;
  xSemaphoreGive(s_lock);
  ESP_LOGE(TAG, "Ethernet sharing failed: %s", esp_err_to_name(error));
}
static bool alive(void) {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  bool fresh = esp_timer_get_time() - s_heartbeat < MASTER_TIMEOUT_US;
  xSemaphoreGive(s_lock);
  return fresh && !atomic_load(&s_stop);
}
static void worker(void *arg) {
  (void)arg;
  esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  esp_netif_t *previous_default = esp_netif_get_default_netif();
  esp_netif_ip_info_t previous_ip = {0}, ip = {0};
  esp_netif_dns_info_t previous_dns = {0}, dns = {0};
  wifi_mode_t previous_mode = WIFI_MODE_NULL;
  bool sharing = false, ap_changed = false, netif_started = false;
  esp_err_t ret = ap ? esp_netif_get_ip_info(ap, &previous_ip) : ESP_ERR_INVALID_STATE;
  if (ret == ESP_OK)
    ret = esp_netif_get_dns_info(ap, ESP_NETIF_DNS_MAIN, &previous_dns);
  if (ret == ESP_OK)
    ret = esp_wifi_get_mode(&previous_mode);
  if (ret != ESP_OK)
    goto cleanup;
  esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
  base.if_key = "USB_ETHERNET";
  base.if_desc = "USB Ethernet uplink";
  base.route_prio = UPLINK_ROUTE_PRIORITY;
  esp_netif_driver_ifconfig_t driver = {
      .handle = &s_netif, .transmit = transmit, .driver_free_rx_buffer = free_rx};
  esp_netif_config_t config = {
      .base = &base, .driver = &driver, .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH};
  s_netif = esp_netif_new(&config);
  if (s_netif == NULL) {
    ret = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  ret = esp_netif_set_mac(s_netif, s_start.mac);
  if (ret != ESP_OK)
    goto cleanup;
  esp_netif_action_start(s_netif, NULL, 0, NULL);
  esp_netif_action_connected(s_netif, NULL, 0, NULL);
  netif_started = true;
  int64_t deadline = esp_timer_get_time() + DHCP_TIMEOUT_US;
  while (alive()) {
    ethernet_router_packet_t packet;
    if (xQueueReceive(s_rx, &packet, pdMS_TO_TICKS(ROUTER_POLL_MS)) == pdTRUE) {
      uint8_t *copy = heap_caps_malloc(packet.length, MALLOC_CAP_SPIRAM);
      if (copy != NULL) {
        memcpy(copy, packet.bytes, packet.length);
        // esp-netif's Ethernet input owns the buffer and invokes free_rx.
        (void)esp_netif_receive(s_netif, copy, packet.length, NULL);
      } else {
        ESP_LOGE(TAG, "RX allocation failed");
      }
    }
    ret = esp_netif_get_ip_info(s_netif, &ip);
    if (ret != ESP_OK)
      break;
    if (ip.ip.addr == 0) {
      if (sharing) {
        ret = ESP_ERR_INVALID_STATE;
        break;
      }
      if (esp_timer_get_time() > deadline) {
        ret = ESP_ERR_TIMEOUT;
        break;
      }
      continue;
    }
    if (!sharing) {
      // Preserve the configured hotspot. Do not silently rewrite credentials or subnets.
      if ((ip.ip.addr & ip.netmask.addr) == (previous_ip.ip.addr & ip.netmask.addr) ||
          (ip.ip.addr & previous_ip.netmask.addr) ==
              (previous_ip.ip.addr & previous_ip.netmask.addr)) {
        ret = ESP_ERR_INVALID_ARG;
        break;
      }
      ret = esp_netif_get_dns_info(s_netif, ESP_NETIF_DNS_MAIN, &dns);
      if (ret != ESP_OK || dns.ip.u_addr.ip4.addr == 0) {
        ret = ESP_ERR_INVALID_RESPONSE;
        break;
      }
      ret = esp_netif_set_default_netif(s_netif);
      if (ret != ESP_OK)
        break;
      esp_netif_dhcps_stop(ap);
      ap_changed = true;
      uint8_t offer = DHCP_OFFER_DNS;
      ret = esp_netif_dhcps_option(
          ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer, sizeof(offer));
      if (ret == ESP_OK)
        ret = esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &dns);
      if (ret == ESP_OK)
        ret = esp_wifi_set_mode(WIFI_MODE_AP);
      if (ret == ESP_OK) {
        wifi_service_start();
        if (!wifi_service_is_active())
          ret = ESP_FAIL;
      }
      if (ret == ESP_OK) {
        esp_err_t dhcp = esp_netif_dhcps_start(ap);
        if (dhcp != ESP_OK && dhcp != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED)
          ret = dhcp;
      }
      if (ret == ESP_OK)
        ret = esp_netif_napt_enable(ap);
      if (ret != ESP_OK)
        break;
      sharing = true;
      ESP_LOGI(TAG, "Ethernet uplink ready; hotspot sharing enabled");
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = SPI_ETHERNET_STATE_SHARING;
    s_status.ip = ip.ip.addr;
    s_status.mask = ip.netmask.addr;
    s_status.gateway = ip.gw.addr;
    xSemaphoreGive(s_lock);
  }
cleanup:
  if (ret != ESP_OK)
    failure(ret);
  if (sharing)
    esp_netif_napt_disable(ap);
  if (ap_changed) {
    wifi_service_stop();
    esp_netif_dhcps_stop(ap);
    esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &previous_dns);
    esp_wifi_set_mode(previous_mode);
  }
  if (s_netif != NULL) {
    if (netif_started) {
      esp_netif_action_disconnected(s_netif, NULL, 0, NULL);
      esp_netif_action_stop(s_netif, NULL, 0, NULL);
    }
    if (previous_default)
      esp_netif_set_default_netif(previous_default);
    esp_netif_destroy(s_netif);
    s_netif = NULL;
  }
  // Keep the diagnostic visible after forwarding and DHCP have stopped.
  if (ret != ESP_OK)
    while (alive())
      vTaskDelay(pdMS_TO_TICKS(ROUTER_POLL_MS));
  xSemaphoreTake(s_lock, portMAX_DELAY);
  vQueueDelete(s_rx);
  vQueueDelete(s_tx);
  s_rx = NULL;
  s_tx = NULL;
  free(s_storage);
  s_storage = NULL;
  s_rx_packet = NULL;
  s_tx_packet = NULL;
  atomic_store(&s_active, false);
  xSemaphoreGive(s_lock);
  vTaskDelete(NULL);
}
spi_status_t ethernet_router_exchange(const uint8_t *payload,
                                      uint16_t length,
                                      uint8_t *out_payload,
                                      uint16_t *out_length) {
  if (out_length == NULL)
    return SPI_STATUS_INVALID_ARG;
  *out_length = 0;
  if (payload == NULL || out_payload == NULL || length < SPI_ETHERNET_REQUEST_META_SIZE ||
      length > SPI_ETHERNET_REQUEST_META_SIZE + SPI_ETHERNET_MTU)
    return SPI_STATUS_INVALID_ARG;
  uint16_t bytes = length - SPI_ETHERNET_REQUEST_META_SIZE;
  uint32_t token;
  memcpy(&token, payload, sizeof(token));
  if (token == 0 || (bytes != 0 && bytes < SPI_ETHERNET_HEADER_SIZE))
    return SPI_STATUS_INVALID_ARG;
  if (s_lock == NULL)
    return SPI_STATUS_NOT_FOUND;
  if (xSemaphoreTake(s_lock, 0) != pdTRUE)
    return SPI_STATUS_BUSY;
  spi_status_t result = SPI_STATUS_NOT_FOUND;
  if (!atomic_load(&s_active) || atomic_load(&s_stop) || token != s_start.token ||
      s_status.state == SPI_ETHERNET_STATE_ERROR)
    goto cleanup;
  result = SPI_STATUS_BUSY;
  if (s_rx_offset != 0 || s_tx_offset != 0)
    goto cleanup;
  s_heartbeat = esp_timer_get_time();
  spi_status_t rx_status = SPI_STATUS_OK;
  if (bytes != 0) {
    s_rx_packet->length = bytes;
    memcpy(s_rx_packet->bytes, payload + SPI_ETHERNET_REQUEST_META_SIZE, bytes);
    if (xQueueSend(s_rx, s_rx_packet, 0) != pdTRUE) {
      ++s_status.dropped;
      rx_status = SPI_STATUS_BUSY;
    }
  }
  memcpy(out_payload, &token, sizeof(token));
  out_payload[sizeof(token)] = (uint8_t)rx_status;
  *out_length = SPI_ETHERNET_REPLY_META_SIZE;
  // Drain TX even when RX is congested, so backpressure cannot stall both directions.
  if (xQueueReceive(s_tx, s_tx_packet, 0) == pdTRUE) {
    memcpy(out_payload + *out_length, s_tx_packet->bytes, s_tx_packet->length);
    *out_length += s_tx_packet->length;
  }
  result = SPI_STATUS_OK;
cleanup:
  xSemaphoreGive(s_lock);
  return result;
}

spi_status_t ethernet_router_execute(
    spi_id_t id, const uint8_t *payload, uint8_t length, uint8_t *out, uint8_t *out_length) {
  *out_length = 0;
  if (length < sizeof(uint32_t))
    return SPI_STATUS_INVALID_ARG;
  uint32_t token;
  memcpy(&token, payload, sizeof(token));
  if (token == 0)
    return SPI_STATUS_INVALID_ARG;
  if (s_lock == NULL)
    s_lock = xSemaphoreCreateMutexStatic(&s_lock_storage);
  if (xSemaphoreTake(s_lock, 0) != pdTRUE)
    return SPI_STATUS_BUSY;
  spi_status_t result = SPI_STATUS_OK;
  if (id == SPI_ID_ETHERNET_START) {
    if (length != sizeof(s_start)) {
      result = SPI_STATUS_INVALID_ARG;
      goto done;
    }
    if (atomic_load(&s_active) || wifi_service_is_active() || wifi_service_is_busy() ||
        grok_wifi_is_active() || session_manager_is_active() || spi_bridge_async_scan_busy()) {
      result = SPI_STATUS_BUSY;
      goto done;
    }
    memcpy(&s_start, payload, sizeof(s_start));
    if (s_start.mac[0] & 1) {
      result = SPI_STATUS_INVALID_ARG;
      goto done;
    }
    s_storage =
        heap_caps_calloc(PACKET_DEPTH * 2 + 2, sizeof(ethernet_router_packet_t), MALLOC_CAP_SPIRAM);
    if (s_storage == NULL) {
      ESP_LOGE(TAG, "Packet queue allocation failed");
      result = SPI_STATUS_ERROR;
      goto done;
    }
    s_rx = xQueueCreateStatic(
        PACKET_DEPTH, sizeof(ethernet_router_packet_t), s_storage, &s_rx_control);
    s_tx = xQueueCreateStatic(PACKET_DEPTH,
                              sizeof(ethernet_router_packet_t),
                              s_storage + PACKET_DEPTH * sizeof(ethernet_router_packet_t),
                              &s_tx_control);
    s_rx_packet = (ethernet_router_packet_t *)s_storage + PACKET_DEPTH * 2;
    s_tx_packet = s_rx_packet + 1;
    s_rx_offset = 0;
    s_tx_offset = 0;
    s_tx_seq = 0;
    s_status = (spi_ethernet_status_t){.token = token, .state = SPI_ETHERNET_STATE_DHCP};
    wifi_config_t ap_config;
    if (esp_wifi_get_config(WIFI_IF_AP, &ap_config) == ESP_OK)
      memcpy(s_status.ssid, ap_config.ap.ssid, sizeof(ap_config.ap.ssid));
    s_heartbeat = esp_timer_get_time();
    atomic_store(&s_stop, false);
    atomic_store(&s_active, true);
    if (xTaskCreatePinnedToCore(
            worker, "eth_router", ROUTER_STACK, NULL, SYS_PRIO_SERVICE_LO, NULL, SYS_CORE_MAIN) !=
        pdPASS) {
      atomic_store(&s_active, false);
      vQueueDelete(s_rx);
      vQueueDelete(s_tx);
      free(s_storage);
      s_storage = NULL;
      result = SPI_STATUS_ERROR;
    }
    goto done;
  }
  if (!atomic_load(&s_active) || token != s_start.token) {
    result = SPI_STATUS_NOT_FOUND;
    goto done;
  }
  s_heartbeat = esp_timer_get_time();
  if (id == SPI_ID_ETHERNET_STOP) {
    atomic_store(&s_stop, true);
  } else if (id == SPI_ID_ETHERNET_STATUS) {
    memcpy(out, &s_status, sizeof(s_status));
    *out_length = sizeof(s_status);
  } else if (id == SPI_ID_ETHERNET_RX) {
    spi_ethernet_fragment_t fragment;
    if (length < sizeof(fragment)) {
      result = SPI_STATUS_INVALID_ARG;
      goto done;
    }
    memcpy(&fragment, payload, sizeof(fragment));
    size_t bytes = length - sizeof(fragment);
    if (fragment.offset == 0) {
      s_rx_offset = 0;
      s_rx_seq = fragment.sequence;
      s_rx_packet->length = fragment.total;
    }
    if (bytes == 0 || bytes > SPI_ETHERNET_CHUNK || fragment.total < ETHERNET_HEADER_SIZE ||
        fragment.total > SPI_ETHERNET_MTU || fragment.offset > fragment.total ||
        fragment.offset != s_rx_offset || fragment.sequence != s_rx_seq ||
        fragment.total != s_rx_packet->length || bytes > fragment.total - fragment.offset) {
      s_rx_offset = 0;
      result = SPI_STATUS_INVALID_ARG;
      goto done;
    }
    memcpy(s_rx_packet->bytes + s_rx_offset, payload + sizeof(fragment), bytes);
    s_rx_offset += bytes;
    if (s_rx_offset == fragment.total) {
      if (xQueueSend(s_rx, s_rx_packet, 0) != pdTRUE) {
        ++s_status.dropped;
        result = SPI_STATUS_BUSY;
      }
      s_rx_offset = 0;
    }
  } else if (id == SPI_ID_ETHERNET_TX) {
    if (s_tx_offset == 0 && xQueueReceive(s_tx, s_tx_packet, 0) != pdTRUE)
      goto done;
    size_t bytes = s_tx_packet->length - s_tx_offset;
    if (bytes > SPI_ETHERNET_CHUNK)
      bytes = SPI_ETHERNET_CHUNK;
    spi_ethernet_fragment_t fragment = {
        .token = token, .sequence = s_tx_seq, .offset = s_tx_offset, .total = s_tx_packet->length};
    memcpy(out, &fragment, sizeof(fragment));
    memcpy(out + sizeof(fragment), s_tx_packet->bytes + s_tx_offset, bytes);
    *out_length = sizeof(fragment) + bytes;
    s_tx_offset += bytes;
    if (s_tx_offset == s_tx_packet->length) {
      s_tx_offset = 0;
      ++s_tx_seq;
    }
  } else
    result = SPI_STATUS_UNSUPPORTED;
done:
  xSemaphoreGive(s_lock);
  return result;
}
