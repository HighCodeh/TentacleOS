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

#include "port_scan.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"

static const char *TAG = "PORT_SCANNER";

static volatile bool s_abort_requested = false;
static port_scan_hit_cb_t s_hit_cb = NULL;
static void *s_hit_ctx = NULL;

void port_scan_set_hit_cb(port_scan_hit_cb_t cb, void *ctx) {
  s_hit_cb = cb;
  s_hit_ctx = ctx;
}

void port_scan_request_abort(void) {
  s_abort_requested = true;
}

void port_scan_reset_abort(void) {
  s_abort_requested = false;
}

#define SCAN_DELAY_MS         5
#define NETWORK_SCAN_DELAY_MS 20
#define FLAG_TYPE_RANGE       0
#define FLAG_TYPE_LIST        1
#define PORT_SCAN_BATCH       8   // concurrent non-blocking connects (LWIP_MAX_SOCKETS=10)
#define BANNER_RECV_MS        250 // brief wait for a banner once a port is open

static void scan_ports_batch(const char *ip,
                             const int *ports,
                             int nports,
                             port_scan_result_t *results,
                             int *count,
                             int max_results);
static bool add_result(const char *ip,
                       int port,
                       port_scan_protocol_t protocol,
                       port_scan_status_t status,
                       char *banner,
                       port_scan_result_t *results,
                       int *count,
                       int max_results);
static int scan_network_iterator(uint32_t ip_start_hbo,
                                 uint32_t ip_end_hbo,
                                 int p_start,
                                 int p_end,
                                 const int *p_list,
                                 int list_size,
                                 int flag_type,
                                 port_scan_result_t *results,
                                 int max_results);
static void
calculate_cidr_bounds(const char *base_ip, int cidr, uint32_t *out_start, uint32_t *out_end);

int port_scan_target_range(const char *target_ip,
                           int start_port,
                           int end_port,
                           port_scan_result_t *results,
                           int max_results) {
  int count = 0;
  int batch[PORT_SCAN_BATCH];
  int nb = 0;
  ESP_LOGI(TAG, "TCP scan %s ports %d-%d", target_ip, start_port, end_port);
  for (int port = start_port; port <= end_port; port++) {
    if (count >= max_results || s_abort_requested)
      break;
    batch[nb++] = port;
    if (nb == PORT_SCAN_BATCH) {
      scan_ports_batch(target_ip, batch, nb, results, &count, max_results);
      nb = 0;
      vTaskDelay(SCAN_DELAY_MS / portTICK_PERIOD_MS);
    }
  }
  if (nb > 0 && count < max_results && !s_abort_requested)
    scan_ports_batch(target_ip, batch, nb, results, &count, max_results);
  return count;
}

int port_scan_target_list(const char *target_ip,
                          const int *port_list,
                          int list_size,
                          port_scan_result_t *results,
                          int max_results) {
  int count = 0;
  int batch[PORT_SCAN_BATCH];
  int nb = 0;
  ESP_LOGI(TAG, "TCP scan %s (%d ports)", target_ip, list_size);
  for (int i = 0; i < list_size; i++) {
    if (count >= max_results || s_abort_requested)
      break;
    batch[nb++] = port_list[i];
    if (nb == PORT_SCAN_BATCH) {
      scan_ports_batch(target_ip, batch, nb, results, &count, max_results);
      nb = 0;
      vTaskDelay(SCAN_DELAY_MS / portTICK_PERIOD_MS);
    }
  }
  if (nb > 0 && count < max_results && !s_abort_requested)
    scan_ports_batch(target_ip, batch, nb, results, &count, max_results);
  return count;
}

int port_scan_network_range_using_port_range(const char *start_ip,
                                             const char *end_ip,
                                             int start_port,
                                             int end_port,
                                             port_scan_result_t *results,
                                             int max_results) {
  uint32_t s = ntohl(inet_addr(start_ip));
  uint32_t e = ntohl(inet_addr(end_ip));
  return scan_network_iterator(
      s, e, start_port, end_port, NULL, 0, FLAG_TYPE_RANGE, results, max_results);
}

int port_scan_network_range_using_port_list(const char *start_ip,
                                            const char *end_ip,
                                            const int *port_list,
                                            int list_size,
                                            port_scan_result_t *results,
                                            int max_results) {
  uint32_t s = ntohl(inet_addr(start_ip));
  uint32_t e = ntohl(inet_addr(end_ip));
  return scan_network_iterator(
      s, e, 0, 0, port_list, list_size, FLAG_TYPE_LIST, results, max_results);
}

int port_scan_cidr_using_port_range(const char *base_ip,
                                    int cidr,
                                    int start_port,
                                    int end_port,
                                    port_scan_result_t *results,
                                    int max_results) {
  uint32_t s, e;
  calculate_cidr_bounds(base_ip, cidr, &s, &e);
  ESP_LOGI(TAG, "CIDR /%d -> Numeric Range Scan", cidr);
  return scan_network_iterator(
      s, e, start_port, end_port, NULL, 0, FLAG_TYPE_RANGE, results, max_results);
}

int port_scan_cidr_using_port_list(const char *base_ip,
                                   int cidr,
                                   const int *port_list,
                                   int list_size,
                                   port_scan_result_t *results,
                                   int max_results) {
  uint32_t s, e;
  calculate_cidr_bounds(base_ip, cidr, &s, &e);
  ESP_LOGI(TAG, "CIDR /%d -> Numeric List Scan", cidr);
  return scan_network_iterator(
      s, e, 0, 0, port_list, list_size, FLAG_TYPE_LIST, results, max_results);
}

// Turn a raw service response into a compact banner. Services like SSH/FTP/SMTP
// announce a line on connect; HTTP only replies to a request, so for an HTTP
// response we pull the Server: header (the useful bit, like nmap shows).
// Otherwise we take the first line. Works on the raw bytes so line boundaries
// (CR/LF) still delimit the value.
static void extract_banner(const char *raw, int rawlen, char *out, int outcap) {
  const char *start = raw;
  int avail = rawlen;
  if (rawlen >= 5 && strncasecmp(raw, "HTTP/", 5) == 0) {
    for (int i = 0; i + 7 < rawlen; i++) {
      if ((i == 0 || raw[i - 1] == '\n') && strncasecmp(&raw[i], "server:", 7) == 0) {
        start = &raw[i + 7];
        while (start < raw + rawlen && *start == ' ')
          start++;
        avail = rawlen - (int)(start - raw);
        break;
      }
    }
  }
  int n = 0;
  while (n < avail && n < outcap - 1 && start[n] != '\r' && start[n] != '\n') {
    char c = start[n];
    out[n] = (c >= 0x20 && c < 0x7F) ? c : '.';
    n++;
  }
  out[n] = '\0';
  if (n == 0)
    strcpy(out, "(Without Banner)");
}

// Scan up to PORT_SCAN_BATCH TCP ports on one IP concurrently: fire all the
// non-blocking connects, then select() once for writability. A socket that
// becomes writable with SO_ERROR 0 is open; SO_ERROR set means closed (RST);
// never writable within the timeout means filtered. This is ~PORT_SCAN_BATCH x
// faster than one connect at a time. UDP connect-scanning was dropped: a silent
// UDP port is indistinguishable from open, so it only produced false positives.
static void scan_ports_batch(const char *ip,
                             const int *ports,
                             int nports,
                             port_scan_result_t *results,
                             int *count,
                             int max_results) {
  int socks[PORT_SCAN_BATCH];
  int sports[PORT_SCAN_BATCH];
  int nsock = 0;
  int maxfd = -1;
  fd_set wset;
  FD_ZERO(&wset);

  char plist[80];
  int pl = 0;
  for (int i = 0; i < nports && i < PORT_SCAN_BATCH && pl < (int)sizeof(plist) - 8; i++)
    pl += snprintf(plist + pl, sizeof(plist) - pl, "%d ", ports[i]);
  ESP_LOGI(TAG, "scanning %s ports: %s", ip, plist);

  struct sockaddr_in dest = {0};
  dest.sin_family = AF_INET;
  dest.sin_addr.s_addr = inet_addr(ip);

  for (int i = 0; i < nports && i < PORT_SCAN_BATCH; i++) {
    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s < 0)
      continue;
    int flags = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, flags | O_NONBLOCK);
    dest.sin_port = htons(ports[i]);
    int err = connect(s, (struct sockaddr *)&dest, sizeof(dest));
    if (err == 0 || errno == EINPROGRESS) {
      socks[nsock] = s;
      sports[nsock] = ports[i];
      nsock++;
      FD_SET(s, &wset);
      if (s > maxfd)
        maxfd = s;
    } else {
      close(s); // immediate failure (e.g. no route)
    }
  }
  if (nsock == 0)
    return;

  struct timeval tv = {.tv_sec = PORT_SCAN_CONNECT_TIMEOUT_S, .tv_usec = 0};
  select(maxfd + 1, NULL, &wset, NULL, &tv);

  for (int i = 0; i < nsock; i++) {
    int s = socks[i];
    int so_error = 0;
    socklen_t l = sizeof(so_error);
    bool open = FD_ISSET(s, &wset) && getsockopt(s, SOL_SOCKET, SO_ERROR, &so_error, &l) == 0 &&
                so_error == 0;
    if (open && *count < max_results) {
      char banner[PORT_SCAN_MAX_BANNER_LEN];
      // Restore blocking so SO_RCVTIMEO bounds the banner grab.
      int flags = fcntl(s, F_GETFL, 0);
      fcntl(s, F_SETFL, flags & ~O_NONBLOCK);
      struct timeval rt = {.tv_sec = 0, .tv_usec = BANNER_RECV_MS * 1000};
      setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof(rt));
      // Passive read first (SSH/FTP/SMTP announce on connect); if nothing, probe
      // with a minimal HTTP GET so web servers reply with their Server header.
      char raw[512];
      int len = recv(s, raw, sizeof(raw) - 1, 0);
      if (len <= 0) {
        static const char http_probe[] = "GET / HTTP/1.0\r\n\r\n";
        send(s, http_probe, sizeof(http_probe) - 1, 0);
        len = recv(s, raw, sizeof(raw) - 1, 0);
      }
      if (len > 0)
        extract_banner(raw, len, banner, sizeof(banner));
      else
        strcpy(banner, "(Without Banner)");
      add_result(
          ip, sports[i], PORT_SCAN_PROTO_TCP, PORT_SCAN_STATUS_OPEN, banner, results, count, max_results);
    }
    close(s);
  }
}

static bool add_result(const char *ip,
                       int port,
                       port_scan_protocol_t protocol,
                       port_scan_status_t status,
                       char *banner,
                       port_scan_result_t *results,
                       int *count,
                       int max_results) {
  if (*count >= max_results)
    return false;
  port_scan_result_t *res = &results[*count];
  strncpy(res->ip_str, ip, 16);
  res->port = port;
  res->protocol = protocol;
  res->status = status;
  strncpy(res->banner, banner, PORT_SCAN_MAX_BANNER_LEN);

  const char *p_str = (protocol == PORT_SCAN_PROTO_TCP) ? "TCP" : "UDP";
  const char *s_str = (status == PORT_SCAN_STATUS_OPEN) ? "OPEN" : "OPEN|FILTERED";
  ESP_LOGI(TAG, "HIT: %s:%d [%s] %s | %s", ip, port, p_str, s_str, banner);
  (*count)++;
  if (s_hit_cb != NULL)
    s_hit_cb(res, s_hit_ctx);
  return true;
}

static int scan_network_iterator(uint32_t ip_start_hbo,
                                 uint32_t ip_end_hbo,
                                 int p_start,
                                 int p_end,
                                 const int *p_list,
                                 int list_size,
                                 int flag_type,
                                 port_scan_result_t *results,
                                 int max_results) {
  int count = 0;
  if (ip_start_hbo > ip_end_hbo) {
    ESP_LOGE(TAG, "IP Start > IP End");
    return 0;
  }

  uint32_t diff = ip_end_hbo - ip_start_hbo;
  if (diff > PORT_SCAN_MAX_IP_RANGE_SPAN) {
    ESP_LOGE(TAG, "Range Error: %u IPs exceeds limit of %d", diff, PORT_SCAN_MAX_IP_RANGE_SPAN);
    return 0;
  }

  ESP_LOGI(TAG, "Scanning %u hosts...", diff + 1);
  uint32_t ip_curr = ip_start_hbo;

  while (ip_curr <= ip_end_hbo && count < max_results && !s_abort_requested) {
    struct in_addr ip_struct;
    ip_struct.s_addr = htonl(ip_curr);
    char current_ip_str[16];
    strcpy(current_ip_str, inet_ntoa(ip_struct));

    int hits = 0;
    if (flag_type == FLAG_TYPE_RANGE)
      hits = port_scan_target_range(
          current_ip_str, p_start, p_end, &results[count], max_results - count);
    else
      hits = port_scan_target_list(
          current_ip_str, p_list, list_size, &results[count], max_results - count);

    count += hits;
    ip_curr++;
    vTaskDelay(NETWORK_SCAN_DELAY_MS / portTICK_PERIOD_MS);
  }
  return count;
}

static void
calculate_cidr_bounds(const char *base_ip, int cidr, uint32_t *out_start, uint32_t *out_end) {
  uint32_t ip_val = ntohl(inet_addr(base_ip));
  uint32_t mask = (cidr == 0) ? 0 : (~0U << (32 - cidr));
  *out_start = ip_val & mask;
  *out_end = *out_start | (~mask);
  if (cidr < 31) {
    (*out_start)++;
    (*out_end)--;
  }
}
