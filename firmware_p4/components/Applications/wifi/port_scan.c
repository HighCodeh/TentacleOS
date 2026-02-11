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

#include "port_scan.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include <string.h>
#include <ctype.h>

static const char *TAG = "PORT_SCANNER";

static void sanitize_banner(char *buffer, int len) {
  for (int i = 0; i < len; i++) {
    if (buffer[i] == '\r' || buffer[i] == '\n') buffer[i] = ' ';
    else if (!isprint((int)buffer[i])) buffer[i] = '.'; 
  }
  buffer[len] = '\0'; }

static bool check_tcp(const char *ip, int port, char *banner_out) {
  struct sockaddr_in dest_addr;
  dest_addr.sin_addr.s_addr = inet_addr(ip);
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(port);

  int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if (sock < 0) return false;

  struct timeval timeout;
  timeout.tv_sec = CONNECT_TIMEOUT_S;
  timeout.tv_usec = 0;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  int err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  if (err != 0) {
    close(sock);
    return false;
  }

  send(sock, "\r\n", 2, 0); 
  int len = recv(sock, banner_out, MAX_BANNER_LEN - 1, 0);

  if (len > 0) {
    sanitize_banner(banner_out, len);
  } else {
    strcpy(banner_out, "(Without Banner)");
  }

  close(sock);
  return true; // open
}

// -1 (Closed), 0 (Open|Filtered), 1 (Open+Data)
static int check_udp(const char *ip, int port, char *banner_out) {
  struct sockaddr_in dest_addr;
  dest_addr.sin_addr.s_addr = inet_addr(ip);
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(port);

  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) return -1;

  struct timeval timeout;
  timeout.tv_sec = 0;
  timeout.tv_usec = UDP_TIMEOUT_MS * 1000;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
    close(sock);
    return -1;
  }

  if (send(sock, "X", 1, 0) < 0) {
    close(sock);
    return -1;
  }

  int len = recv(sock, banner_out, MAX_BANNER_LEN - 1, 0);
  int result = -1;

  if (len > 0) {
    result = 1; 
    sanitize_banner(banner_out, len);
  } else {
    strcpy(banner_out, "(Without Banner)");
    if (errno == ECONNREFUSED) {
      result = -1; 
    } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
      result = 0; 
    }
  }
  close(sock);
  return result;
}

static bool add_result(const char *ip, int port, int scan_type, char *banner, scan_result_t *results, int *count, int max_results) {
  if (*count >= max_results) return false;
  scan_result_t *res = &results[*count];
  strncpy(res->ip_str, ip, 16);
  res->port = port;
  res->protocol = (scan_type == 0) ? PROTO_TCP : PROTO_UDP;
  strncpy(res->banner, banner, MAX_BANNER_LEN);
  res->status = (scan_type == 0 || scan_type == 1) ? STATUS_OPEN : STATUS_OPEN_FILTERED;

  const char *p_str = (scan_type == 0) ? "TCP" : "UDP";
  const char *s_str = (res->status == STATUS_OPEN) ? "OPEN" : "FILTERED";
  ESP_LOGI(TAG, "HIT: %s:%d [%s] %s | %s", ip, port, p_str, s_str, banner);
  (*count)++;
  return true;
}

int port_scan_target_range(const char *target_ip, int start_port, int end_port, scan_result_t *results, int max_results) {
  int count = 0;
  char banner[MAX_BANNER_LEN];
  for (int port = start_port; port <= end_port; port++) {
    if (count >= max_results) break;
    if (check_tcp(target_ip, port, banner)) add_result(target_ip, port, 0, banner, results, &count, max_results);
    if (count >= max_results) break;
    int udp = check_udp(target_ip, port, banner);
    if (udp >= 0) add_result(target_ip, port, udp, banner, results, &count, max_results);
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  return count;
}

int port_scan_target_list(const char *target_ip, const int *port_list, int list_size, scan_result_t *results, int max_results) {
  int count = 0;
  char banner[MAX_BANNER_LEN];
  for (int i = 0; i < list_size; i++) {
    int port = port_list[i];
    if (count >= max_results) break;
    if (check_tcp(target_ip, port, banner)) add_result(target_ip, port, 0, banner, results, &count, max_results);
    if (count >= max_results) break;
    int udp = check_udp(target_ip, port, banner);
    if (udp >= 0) add_result(target_ip, port, udp, banner, results, &count, max_results);
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  return count;
}

static int scan_network_iterator_raw(uint32_t ip_start_hbo, uint32_t ip_end_hbo, 
                                     int p_start, int p_end, const int *p_list, int list_size, 
                                     int flag_type, 
                                     scan_result_t *results, int max_results) {
  int count = 0;
  if (ip_start_hbo > ip_end_hbo) { ESP_LOGE(TAG, "IP Start > IP End"); return 0; }

  uint32_t diff = ip_end_hbo - ip_start_hbo;
  if (diff > MAX_IP_RANGE_SPAN) {
    ESP_LOGE(TAG, "Range Error: %u IPs exceeds limit of %d", diff, MAX_IP_RANGE_SPAN);
    return 0;
  }

  ESP_LOGI(TAG, "Scanning %u hosts...", diff + 1);
  uint32_t ip_curr = ip_start_hbo;

  while (ip_curr <= ip_end_hbo && count < max_results) {
    struct in_addr ip_struct;
    ip_struct.s_addr = htonl(ip_curr);
    char current_ip_str[16];
    strcpy(current_ip_str, inet_ntoa(ip_struct));

    int hits = 0;
    if (flag_type == 0) hits = port_scan_target_range(current_ip_str, p_start, p_end, &results[count], max_results - count);
    else hits = port_scan_target_list(current_ip_str, p_list, list_size, &results[count], max_results - count);

    count += hits;
    ip_curr++;
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
  return count;
}

int port_scan_network_range_using_port_range(const char *start_ip, const char *end_ip, int start_port, int end_port, scan_result_t *results, int max_results) {
  uint32_t s = ntohl(inet_addr(start_ip));
  uint32_t e = ntohl(inet_addr(end_ip));
  return scan_network_iterator_raw(s, e, start_port, end_port, NULL, 0, 0, results, max_results);
}

int port_scan_network_range_using_port_list(const char *start_ip, const char *end_ip, const int *port_list, int list_size, scan_result_t *results, int max_results) {
  uint32_t s = ntohl(inet_addr(start_ip));
  uint32_t e = ntohl(inet_addr(end_ip));
  return scan_network_iterator_raw(s, e, 0, 0, port_list, list_size, 1, results, max_results);
}

static void calculate_cidr_bounds(const char *base_ip, int cidr, uint32_t *start_out, uint32_t *end_out) {
  uint32_t ip_val = ntohl(inet_addr(base_ip));
  uint32_t mask = (cidr == 0) ? 0 : (~0U << (32 - cidr));
  *start_out = ip_val & mask;
  *end_out = *start_out | (~mask);
  if (cidr < 31) { (*start_out)++; (*end_out)--; }
}

int port_scan_cidr_using_port_range(const char *base_ip, int cidr, int start_port, int end_port, scan_result_t *results, int max_results) {
  uint32_t s, e;
  calculate_cidr_bounds(base_ip, cidr, &s, &e);
  ESP_LOGI(TAG, "CIDR /%d -> Numeric Range Scan", cidr);
  return scan_network_iterator_raw(s, e, start_port, end_port, NULL, 0, 0, results, max_results);
}

int port_scan_cidr_using_port_list(const char *base_ip, int cidr, const int *port_list, int list_size, scan_result_t *results, int max_results) {
  uint32_t s, e;
  calculate_cidr_bounds(base_ip, cidr, &s, &e);
  ESP_LOGI(TAG, "CIDR /%d -> Numeric List Scan", cidr);
  return scan_network_iterator_raw(s, e, 0, 0, port_list, list_size, 1, results, max_results);
}
