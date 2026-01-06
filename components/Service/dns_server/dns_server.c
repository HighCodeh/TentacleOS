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


#include "esp_log.h"
#include "lwip/sockets.h" 
#include "dns_server.h"
#include "lwip/netdb.h"   
#include <string.h>       
#include <ctype.h>
#include "esp_netif.h"    

static const char *TAG_DNS = "dns_server";

static TaskHandle_t dns_task_handle = NULL;

// DNS Header Structure
typedef struct __attribute__((packed)) {
  uint16_t id;       
  uint16_t flags;    
  uint16_t qdcount;  // Questions count
  uint16_t ancount;  // Answers count
  uint16_t nscount;  // Authority records count
  uint16_t arcount;  // Additional records count
} dns_header_t;

// Question Tail Structure (Type and Class)
typedef struct __attribute__((packed)) {
  uint16_t qtype;  // QTYPE (A=1 for IPv4)
  uint16_t qclass; // QCLASS (IN=1 for Internet)
} dns_question_tail_t;

// DNS Answer Structure (for A record)
typedef struct __attribute__((packed)) {
  uint16_t name;     // Compression pointer (0xC0xx)
  uint16_t type;     // Resource Type (A=1)
  uint16_t class;    // Class (IN=1)
  uint32_t ttl;      // Time to Live
  uint16_t data_len; // Data length (4 for IPv4)
  uint32_t ip_addr;  // IP Address (network byte order)
} dns_answer_t;

// Helper to parse domain name from query
// Returns linear size in bytes (including labels and terminator) or 0 on error
static int parse_dns_name(uint8_t *buffer, int len, int offset, char *out_name, int out_len) {
  int ptr = offset;
  int out_ptr = 0;
  int jumped = 0;
  int count = 0; // Infinite loop protection

  if (out_name) out_name[0] = '\0';

  while (ptr < len) {
    uint8_t c = buffer[ptr];

    // End of name
    if (c == 0) {
      ptr++;
      if (!jumped) return ptr - offset;
      return -1; 
    }

    // Compression pointer (0xC0)
    if ((c & 0xC0) == 0xC0) {
      if (ptr + 1 >= len) return 0; // Buffer overflow
      // Standard queries usually don't use compression in the initial QNAME.
      // Treating as error/end of linear section for size calculation.
      return 0; 
    }

    // Standard Label
    int label_len = c;
    ptr++;

    if (ptr + label_len >= len) return 0; // Buffer overflow

    if (out_name && out_ptr < out_len - 1) {
      if (out_ptr > 0) out_name[out_ptr++] = '.';
      for (int i = 0; i < label_len; i++) {
        if (out_ptr < out_len - 1) {
          char ch = buffer[ptr + i];
          // Basic sanitization for logs
          out_name[out_ptr++] = isprint(ch) ? ch : '?';
        }
      }
      out_name[out_ptr] = '\0';
    }
    ptr += label_len;
    count++;
    if (count > 20) return 0; // Loop check
  }
  return 0; 
}

static void send_dns_response(int sock, struct sockaddr_in *client_addr, socklen_t addr_len,
                              uint8_t *request_buf, int request_len) {

  uint8_t response_buf[512];
  if (request_len > sizeof(response_buf)) return; // Safety check

  memset(response_buf, 0, sizeof(response_buf));

  // 1. Validate Header and Size
  if (request_len < sizeof(dns_header_t)) return;

  // Get dynamic IP from AP Interface
  esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  esp_netif_ip_info_t ip_info;

  // Fallback IP
  uint32_t target_ip = inet_addr("192.168.4.1");

  if (netif) {
    esp_netif_get_ip_info(netif, &ip_info);
    target_ip = ip_info.ip.addr;
  } else {
    ESP_LOGW(TAG_DNS, "Could not get WIFI_AP_DEF handle, using default IP.");
  }

  // 2. Parse Question for Log and validation
  char domain_name[128] = {0};
  int qname_len = parse_dns_name(request_buf, request_len, sizeof(dns_header_t), domain_name, sizeof(domain_name));

  if (qname_len <= 0) {
    ESP_LOGE(TAG_DNS, "Malformed or invalid domain name.");
    return; 
  }

  // Verify if question tail fits
  if (sizeof(dns_header_t) + qname_len + sizeof(dns_question_tail_t) > request_len) {
    ESP_LOGE(TAG_DNS, "Truncated packet in question section.");
    return;
  }

  ESP_LOGI(TAG_DNS, "DNS Query for: %s", domain_name);

  // 3. Build Response
  memcpy(response_buf, request_buf, sizeof(dns_header_t));
  dns_header_t *resp_hdr = (dns_header_t *)response_buf;

      // DNS Flags for Evil Twin (Authoritative)
      // 0x8500: QR=1, Opcode=0, AA=1 (Auth), TC=0, RD=1, RA=0, Rcode=0
      resp_hdr->flags = htons(0x8500); 
      
      resp_hdr->ancount = htons(1);  resp_hdr->nscount = 0;
  resp_hdr->arcount = 0;

  // Copy Question (Required in response)
  int question_full_len = qname_len + sizeof(dns_question_tail_t);
  memcpy(response_buf + sizeof(dns_header_t), request_buf + sizeof(dns_header_t), question_full_len);

  // Add Answer
  dns_answer_t *answer = (dns_answer_t *)(response_buf + sizeof(dns_header_t) + question_full_len);

  // Pointer to name (0xC00C points to start of question in header offset 12)
  answer->name = htons(0xC00C); 
  answer->type = htons(1);      // A (IPv4)
  answer->class = htons(1);     // IN
  answer->ttl = htonl(60);      // 60s
  answer->data_len = htons(4);  // IPv4 size
  answer->ip_addr = target_ip;  

  // Send
  int response_total_len = sizeof(dns_header_t) + question_full_len + sizeof(dns_answer_t);

  sendto(sock, response_buf, response_total_len, 0, (struct sockaddr *)client_addr, addr_len);
}

// Main Task
void dns_server_task(void *pvParameters) {
  uint8_t rx_buffer[512];
  struct sockaddr_in server_addr;
  struct sockaddr_in client_addr;
  socklen_t addr_len = sizeof(client_addr);
  int sock;

  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(53);
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) {
    ESP_LOGE(TAG_DNS, "Failed to create socket: %d", errno);
    vTaskDelete(NULL);
    return;
  }

  struct timeval tv;
  tv.tv_sec = 1; // Timeout for graceful shutdown check
  tv.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
    ESP_LOGE(TAG_DNS, "Failed to bind socket: %d", errno);
    close(sock);
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(TAG_DNS, "DNS Server started (Authoritative 0x8500) - IPv4");

  while (1) {
    int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer), 0, (struct sockaddr *)&client_addr, &addr_len);

    if (len > 0) {
      // Basic malformation check
      if (len < sizeof(dns_header_t)) {
        ESP_LOGW(TAG_DNS, "Packet discarded (too small): %d bytes", len);
        continue;
      }

      dns_header_t *header = (dns_header_t *)rx_buffer;
      uint16_t qdcount = ntohs(header->qdcount);

      // Ignore if not a query (QR bit 0) or no questions
      if ((header->flags & 0x8000) || qdcount == 0) {
        continue;
      }

      send_dns_response(sock, &client_addr, addr_len, rx_buffer, len);
    }
  }

  close(sock);
  vTaskDelete(NULL);
}

void start_dns_server(void) {
  if (dns_task_handle != NULL) {
    ESP_LOGW(TAG_DNS, "DNS server is already running");
    return;
  }
  // Stack increased to 4096
  xTaskCreate(&dns_server_task, "dns_server", 4096, NULL, 5, &dns_task_handle);
}

void stop_dns_server(void) {
  if (dns_task_handle != NULL) {
    vTaskDelete(dns_task_handle);
    dns_task_handle = NULL;
    ESP_LOGI(TAG_DNS, "DNS Server stopped");
  }
}


