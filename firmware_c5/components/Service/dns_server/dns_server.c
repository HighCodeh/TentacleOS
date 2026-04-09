// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "dns_server.h"

#include <ctype.h>
#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

static const char *TAG = "DNS_SERVER";

#define DNS_PORT                53
#define DNS_BUF_SIZE            512
#define DNS_TASK_STACK_SIZE     4096
#define DNS_TASK_PRIORITY       5
#define DNS_RECV_TIMEOUT_S      1
#define DNS_RESPONSE_TTL        60
#define DNS_FALLBACK_IP         "192.168.4.1"
#define DNS_MAX_DOMAIN_LEN      128
#define DNS_MAX_LABEL_DEPTH     20
#define DNS_FLAGS_AUTH_RESPONSE 0x8500
#define DNS_COMPRESSION_PTR     0xC00C
#define DNS_TYPE_A              1
#define DNS_CLASS_IN            1
#define DNS_IPV4_LEN            4

typedef struct __attribute__((packed)) {
  uint16_t id;
  uint16_t flags;
  uint16_t qdcount;
  uint16_t ancount;
  uint16_t nscount;
  uint16_t arcount;
} dns_server_header_t;

typedef struct __attribute__((packed)) {
  uint16_t qtype;
  uint16_t qclass;
} dns_server_question_tail_t;

typedef struct __attribute__((packed)) {
  uint16_t name;
  uint16_t type;
  uint16_t class;
  uint32_t ttl;
  uint16_t data_len;
  uint32_t ip_addr;
} dns_server_answer_t;

static TaskHandle_t s_task_handle = NULL;

static int parse_dns_name(uint8_t *buffer, int len, int offset, char *out_name, int out_len);
static void send_dns_response(int sock,
                              struct sockaddr_in *client_addr,
                              socklen_t addr_len,
                              uint8_t *request_buf,
                              int request_len);
static void dns_server_task(void *pvParameters);

void start_dns_server(void) {
  if (s_task_handle != NULL) {
    ESP_LOGW(TAG, "DNS server already running");
    return;
  }
  xTaskCreate(
      dns_server_task, "dns_server", DNS_TASK_STACK_SIZE, NULL, DNS_TASK_PRIORITY, &s_task_handle);
}

void stop_dns_server(void) {
  if (s_task_handle != NULL) {
    vTaskDelete(s_task_handle);
    s_task_handle = NULL;
    ESP_LOGI(TAG, "DNS server stopped");
  }
}

static int parse_dns_name(uint8_t *buffer, int len, int offset, char *out_name, int out_len) {
  int ptr = offset;
  int out_ptr = 0;
  int count = 0;

  if (out_name != NULL) {
    out_name[0] = '\0';
  }

  while (ptr < len) {
    uint8_t c = buffer[ptr];

    if (c == 0) {
      ptr++;
      return ptr - offset;
    }

    // Compression pointer (0xC0)
    if ((c & 0xC0) == 0xC0) {
      if (ptr + 1 >= len) {
        return 0;
      }
      return 0;
    }

    int label_len = c;
    ptr++;

    if (ptr + label_len >= len) {
      return 0;
    }

    if (out_name != NULL && out_ptr < out_len - 1) {
      if (out_ptr > 0) {
        out_name[out_ptr++] = '.';
      }
      for (int i = 0; i < label_len; i++) {
        if (out_ptr < out_len - 1) {
          char ch = buffer[ptr + i];
          out_name[out_ptr++] = isprint(ch) ? ch : '?';
        }
      }
      out_name[out_ptr] = '\0';
    }
    ptr += label_len;
    count++;
    if (count > DNS_MAX_LABEL_DEPTH) {
      return 0;
    }
  }
  return 0;
}

static void send_dns_response(int sock,
                              struct sockaddr_in *client_addr,
                              socklen_t addr_len,
                              uint8_t *request_buf,
                              int request_len) {
  uint8_t response_buf[DNS_BUF_SIZE];
  if (request_len > (int)sizeof(response_buf)) {
    return;
  }

  memset(response_buf, 0, sizeof(response_buf));

  if (request_len < (int)sizeof(dns_server_header_t)) {
    return;
  }

  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  esp_netif_ip_info_t ip_info;
  uint32_t target_ip = inet_addr(DNS_FALLBACK_IP);

  if (netif != NULL) {
    esp_netif_get_ip_info(netif, &ip_info);
    target_ip = ip_info.ip.addr;
  } else {
    ESP_LOGW(TAG, "Could not get AP handle, using fallback IP");
  }

  char domain_name[DNS_MAX_DOMAIN_LEN] = {0};
  int qname_len = parse_dns_name(
      request_buf, request_len, sizeof(dns_server_header_t), domain_name, sizeof(domain_name));

  if (qname_len <= 0) {
    ESP_LOGE(TAG, "Malformed domain name");
    return;
  }

  if (sizeof(dns_server_header_t) + qname_len + sizeof(dns_server_question_tail_t) >
      (size_t)request_len) {
    ESP_LOGE(TAG, "Truncated question section");
    return;
  }

  ESP_LOGI(TAG, "DNS query: %s", domain_name);

  memcpy(response_buf, request_buf, sizeof(dns_server_header_t));
  dns_server_header_t *resp_hdr = (dns_server_header_t *)response_buf;
  resp_hdr->flags = htons(DNS_FLAGS_AUTH_RESPONSE);
  resp_hdr->ancount = htons(1);
  resp_hdr->nscount = 0;
  resp_hdr->arcount = 0;

  int question_full_len = qname_len + sizeof(dns_server_question_tail_t);
  memcpy(response_buf + sizeof(dns_server_header_t),
         request_buf + sizeof(dns_server_header_t),
         question_full_len);

  dns_server_answer_t *answer =
      (dns_server_answer_t *)(response_buf + sizeof(dns_server_header_t) + question_full_len);
  answer->name = htons(DNS_COMPRESSION_PTR);
  answer->type = htons(DNS_TYPE_A);
  answer->class = htons(DNS_CLASS_IN);
  answer->ttl = htonl(DNS_RESPONSE_TTL);
  answer->data_len = htons(DNS_IPV4_LEN);
  answer->ip_addr = target_ip;

  int response_total_len =
      sizeof(dns_server_header_t) + question_full_len + sizeof(dns_server_answer_t);
  sendto(sock, response_buf, response_total_len, 0, (struct sockaddr *)client_addr, addr_len);
}

static void dns_server_task(void *pvParameters) {
  uint8_t rx_buffer[DNS_BUF_SIZE];
  struct sockaddr_in server_addr;
  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(DNS_PORT);
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) {
    ESP_LOGE(TAG, "Failed to create socket: %d", errno);
    vTaskDelete(NULL);
    return;
  }

  struct timeval tv = {
      .tv_sec = DNS_RECV_TIMEOUT_S,
      .tv_usec = 0,
  };
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    ESP_LOGE(TAG, "Failed to bind socket: %d", errno);
    close(sock);
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(TAG, "DNS server started on port %d", DNS_PORT);

  while (1) {
    int len =
        recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&client_addr, &addr_len);

    if (len > 0) {
      if (len < (int)sizeof(dns_server_header_t)) {
        ESP_LOGW(TAG, "Packet too small: %d bytes", len);
        continue;
      }

      dns_server_header_t *header = (dns_server_header_t *)rx_buffer;
      uint16_t qdcount = ntohs(header->qdcount);

      if ((header->flags & 0x8000) || qdcount == 0) {
        continue;
      }

      send_dns_response(sock, &client_addr, addr_len, rx_buffer, len);
    }
  }

  close(sock);
  vTaskDelete(NULL);
}
