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

#include "wifi_udp_scan.h"

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "lwip/inet_chksum.h"
#include "lwip/sockets.h"

#define BATCH_PORTS               4
#define REPLY_BYTES               512
#define ATTEMPTS                  2
#define REPLY_WINDOW_US           500000
#define POLL_US                   50000
#define REQUEST_BYTES             48
#define DNS_PORT                  53
#define NTP_PORT                  123
#define DNS_REQUEST_BYTES         17
#define DNS_QUESTION_COUNT_LOW    5
#define DNS_TYPE_LOW              14
#define DNS_CLASS_LOW             16
#define DNS_TYPE_NS               2
#define DNS_CLASS_IN              1
#define NTP_CLIENT_V4             0x23
#define NTP_TRANSMIT_OFFSET       40
#define IPV4_HEADER_MIN           20
#define IPV4_VERSION              4
#define IPV4_VERSION_SHIFT        4
#define IPV4_IHL_MASK             15
#define IPV4_WORD_BYTES           4
#define IPV4_LENGTH_OFFSET        2
#define IPV4_FRAGMENT_OFFSET      6
#define IPV4_FRAGMENT_MASK        0x3fff
#define IPV4_PROTOCOL_OFFSET      9
#define IPV4_SOURCE_OFFSET        12
#define IPV4_TARGET_OFFSET        16
#define ICMP_HEADER_BYTES         8
#define ICMP_UNREACHABLE          3
#define ICMP_NET_UNREACHABLE      0
#define ICMP_HOST_UNREACHABLE     1
#define ICMP_PROTOCOL_UNREACHABLE 2
#define ICMP_PORT_UNREACHABLE     3
#define ICMP_NET_PROHIBITED       9
#define ICMP_HOST_PROHIBITED      10
#define ICMP_ADMIN_PROHIBITED     13
#define UDP_HEADER_BYTES          8
#define UDP_TARGET_PORT_OFFSET    2
#define UDP_LENGTH_OFFSET         4
#define BYTE_BITS                 8
#define PRINTABLE_FIRST           0x20
#define PRINTABLE_END             0x7f

typedef struct {
  int32_t socket_fd;
  uint16_t port;
  uint16_t local_port;
  uint32_t local_ip;
  int64_t deadline;
  uint8_t attempts;
  bool is_done;
  uint8_t payload[REQUEST_BYTES];
  size_t payload_size;
} wifi_udp_scan_port_t;

typedef struct {
  int32_t icmp_fd;
  uint32_t target_ip;
  uint8_t count;
  wifi_udp_scan_port_t ports[BATCH_PORTS];
  uint8_t reply[REPLY_BYTES];
  wifi_udp_scan_result_t result;
} wifi_udp_scan_batch_t;

static const char *TAG = "WIFI_UDP_SCAN";
static atomic_bool s_is_cancelled;

static bool transient_error(void);
static uint16_t read_u16(const uint8_t *bytes);
static bool nonblocking(int32_t socket_fd);
static void prepare_payload(wifi_udp_scan_port_t *port);
static esp_err_t send_probe(wifi_udp_scan_port_t *port);
static void
classify_icmp(wifi_udp_scan_batch_t *batch, size_t length, const wifi_udp_scan_request_t *request);
static esp_err_t scan_batch(wifi_udp_scan_batch_t *batch, const wifi_udp_scan_request_t *request);
static void report(wifi_udp_scan_batch_t *batch,
                   const wifi_udp_scan_request_t *request,
                   wifi_udp_scan_port_t *port,
                   int32_t length);

void wifi_udp_scan_cancel(void) {
  atomic_store(&s_is_cancelled, true);
}

void wifi_udp_scan_reset(void) {
  atomic_store(&s_is_cancelled, false);
}

esp_err_t wifi_udp_scan_run(const wifi_udp_scan_request_t *request) {
  struct in_addr address;
  if (request == NULL || request->ip == NULL || request->ports == NULL ||
      request->on_result == NULL || request->count == 0 ||
      request->count > WIFI_UDP_SCAN_MAX_PORTS || inet_pton(AF_INET, request->ip, &address) != 1)
    return ESP_ERR_INVALID_ARG;
  for (uint16_t i = 0; i < request->count; ++i)
    if (request->ports[i] <= 0 || request->ports[i] > UINT16_MAX)
      return ESP_ERR_INVALID_ARG;
  if (atomic_load(&s_is_cancelled))
    return ESP_ERR_INVALID_STATE;
  esp_err_t error = ESP_ERR_NO_MEM;
  wifi_udp_scan_batch_t *batch =
      heap_caps_calloc(1, sizeof(*batch), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (batch == NULL) {
    ESP_LOGE(TAG, "Cannot allocate UDP scan workspace in PSRAM");
    goto cleanup;
  }
  batch->icmp_fd = -1;
  for (uint8_t i = 0; i < BATCH_PORTS; ++i)
    batch->ports[i].socket_fd = -1;
  batch->target_ip = address.s_addr;
  error = ESP_FAIL;
  batch->icmp_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (batch->icmp_fd < 0 || !nonblocking(batch->icmp_fd))
    goto cleanup;
  for (uint16_t first = 0; first < request->count; first += BATCH_PORTS) {
    if (atomic_load(&s_is_cancelled)) {
      error = ESP_ERR_INVALID_STATE;
      goto cleanup;
    }
    batch->count = request->count - first < BATCH_PORTS ? request->count - first : BATCH_PORTS;
    for (uint8_t i = 0; i < batch->count; ++i) {
      if (atomic_load(&s_is_cancelled)) {
        error = ESP_ERR_INVALID_STATE;
        goto cleanup;
      }
      wifi_udp_scan_port_t *port = &batch->ports[i];
      *port = (wifi_udp_scan_port_t){.socket_fd = -1, .port = request->ports[first + i]};
      struct sockaddr_in target = {.sin_family = AF_INET, .sin_port = htons(port->port)};
      target.sin_addr = address;
      error = ESP_FAIL;
      port->socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
      if (port->socket_fd < 0 || !nonblocking(port->socket_fd) ||
          connect(port->socket_fd, (struct sockaddr *)&target, sizeof(target)) != 0)
        goto cleanup;
      struct sockaddr_in local = {0};
      socklen_t local_size = sizeof(local);
      if (getsockname(port->socket_fd, (struct sockaddr *)&local, &local_size) != 0 ||
          local_size < sizeof(local) || local.sin_port == 0)
        goto cleanup;
      port->local_port = ntohs(local.sin_port);
      port->local_ip = local.sin_addr.s_addr;
      prepare_payload(port);
      error = send_probe(port);
      if (error != ESP_OK)
        goto cleanup;
    }
    error = scan_batch(batch, request);
    for (uint8_t i = 0; i < batch->count; ++i) {
      close(batch->ports[i].socket_fd);
      batch->ports[i].socket_fd = -1;
    }
    if (error != ESP_OK)
      goto cleanup;
  }
cleanup:
  if (batch != NULL) {
    if (batch->icmp_fd >= 0)
      close(batch->icmp_fd);
    for (uint8_t i = 0; i < BATCH_PORTS; ++i)
      if (batch->ports[i].socket_fd >= 0)
        close(batch->ports[i].socket_fd);
    free(batch);
  }
  if (error != ESP_OK && error != ESP_ERR_INVALID_STATE)
    ESP_LOGE(TAG, "UDP scan failed: %s", esp_err_to_name(error));
  return error;
}

static bool transient_error(void) {
  return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}

static uint16_t read_u16(const uint8_t *bytes) {
  return ((uint16_t)bytes[0] << BYTE_BITS) | bytes[1];
}

static bool nonblocking(int32_t socket_fd) {
  int32_t flags = fcntl(socket_fd, F_GETFL, 0);
  return flags >= 0 && fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static void prepare_payload(wifi_udp_scan_port_t *port) {
  if (port->port == DNS_PORT) {
    uint16_t identifier = (uint16_t)esp_random();
    memcpy(port->payload, &identifier, sizeof(identifier));
    port->payload[DNS_QUESTION_COUNT_LOW] = 1;
    port->payload[DNS_TYPE_LOW] = DNS_TYPE_NS;
    port->payload[DNS_CLASS_LOW] = DNS_CLASS_IN;
    port->payload_size = DNS_REQUEST_BYTES;
  } else if (port->port == NTP_PORT) {
    port->payload[0] = NTP_CLIENT_V4;
    for (size_t i = NTP_TRANSMIT_OFFSET; i < REQUEST_BYTES; i += sizeof(uint32_t)) {
      uint32_t nonce = esp_random();
      memcpy(port->payload + i, &nonce, sizeof(nonce));
    }
    port->payload_size = REQUEST_BYTES;
  }
}

static esp_err_t send_probe(wifi_udp_scan_port_t *port) {
  int32_t sent = send(port->socket_fd, port->payload, port->payload_size, 0);
  if (sent < 0 || (size_t)sent != port->payload_size) {
    if (sent < 0 && errno == ECONNREFUSED) {
      port->is_done = true;
      return ESP_OK;
    }
    return ESP_FAIL;
  }
  ++port->attempts;
  port->deadline = esp_timer_get_time() + REPLY_WINDOW_US;
  return ESP_OK;
}

static void report(wifi_udp_scan_batch_t *batch,
                   const wifi_udp_scan_request_t *request,
                   wifi_udp_scan_port_t *port,
                   int32_t length) {
  batch->result = (wifi_udp_scan_result_t){
      .port = port->port, .status = length >= 0 ? WIFI_UDP_SCAN_OPEN : WIFI_UDP_SCAN_OPEN_FILTERED};
  if (length > 0) {
    size_t count = (size_t)length < sizeof(batch->result.banner) - 1
                       ? (size_t)length
                       : sizeof(batch->result.banner) - 1;
    for (size_t i = 0; i < count; ++i) {
      uint8_t byte = batch->reply[i];
      batch->result.banner[i] = byte >= PRINTABLE_FIRST && byte < PRINTABLE_END ? (char)byte : '.';
    }
  }
  port->is_done = true;
  request->on_result(&batch->result, request->context);
}

static esp_err_t scan_batch(wifi_udp_scan_batch_t *batch, const wifi_udp_scan_request_t *request) {
  while (!atomic_load(&s_is_cancelled)) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(batch->icmp_fd, &readable);
    int32_t max_fd = batch->icmp_fd;
    uint8_t pending = 0;
    for (uint8_t i = 0; i < batch->count; ++i) {
      wifi_udp_scan_port_t *port = &batch->ports[i];
      if (port->is_done)
        continue;
      ++pending;
      FD_SET(port->socket_fd, &readable);
      if (port->socket_fd > max_fd)
        max_fd = port->socket_fd;
    }
    if (pending == 0)
      return ESP_OK;
    struct timeval timeout = {.tv_usec = POLL_US};
    int32_t ready = select(max_fd + 1, &readable, NULL, NULL, &timeout);
    if (ready < 0) {
      if (errno == EINTR)
        FD_ZERO(&readable);
      else
        return ESP_FAIL;
    }
    // An actual UDP reply is stronger evidence than an asynchronous ICMP error.
    for (uint8_t i = 0; i < batch->count; ++i) {
      wifi_udp_scan_port_t *port = &batch->ports[i];
      if (port->is_done || !FD_ISSET(port->socket_fd, &readable))
        continue;
      int32_t length = recv(port->socket_fd, batch->reply, sizeof(batch->reply), 0);
      if (length >= 0)
        report(batch, request, port, length);
      else if (errno == ECONNREFUSED)
        port->is_done = true;
      else if (!transient_error())
        return ESP_FAIL;
    }
    if (FD_ISSET(batch->icmp_fd, &readable)) {
      int32_t length = recv(batch->icmp_fd, batch->reply, sizeof(batch->reply), 0);
      if (length >= 0)
        classify_icmp(batch, (size_t)length, request);
      else if (!transient_error())
        return ESP_FAIL;
    }
    for (uint8_t i = 0; i < batch->count && !atomic_load(&s_is_cancelled); ++i) {
      wifi_udp_scan_port_t *port = &batch->ports[i];
      if (port->is_done || esp_timer_get_time() < port->deadline)
        continue;
      if (port->attempts == ATTEMPTS)
        report(batch, request, port, -1);
      else {
        esp_err_t error = send_probe(port);
        if (error != ESP_OK)
          return error;
      }
    }
  }
  return ESP_ERR_INVALID_STATE;
}

static void
classify_icmp(wifi_udp_scan_batch_t *batch, size_t length, const wifi_udp_scan_request_t *request) {
  const uint8_t *ip = batch->reply;
  if (length < IPV4_HEADER_MIN || ip[0] >> IPV4_VERSION_SHIFT != IPV4_VERSION ||
      ip[IPV4_PROTOCOL_OFFSET] != IPPROTO_ICMP)
    return;
  size_t outer = (ip[0] & IPV4_IHL_MASK) * IPV4_WORD_BYTES;
  size_t total = read_u16(ip + IPV4_LENGTH_OFFSET);
  if (outer < IPV4_HEADER_MIN || total > length ||
      total < outer + ICMP_HEADER_BYTES + IPV4_HEADER_MIN + UDP_HEADER_BYTES ||
      (read_u16(ip + IPV4_FRAGMENT_OFFSET) & IPV4_FRAGMENT_MASK) != 0)
    return;
  const uint8_t *icmp = ip + outer;
  if (icmp[0] != ICMP_UNREACHABLE || inet_chksum(icmp, total - outer) != 0)
    return;
  // These unreachable codes prove rejection, not an open or silent UDP port.
  switch (icmp[1]) {
    case ICMP_NET_UNREACHABLE:
    case ICMP_HOST_UNREACHABLE:
    case ICMP_PROTOCOL_UNREACHABLE:
    case ICMP_PORT_UNREACHABLE:
    case ICMP_NET_PROHIBITED:
    case ICMP_HOST_PROHIBITED:
    case ICMP_ADMIN_PROHIBITED:
      break;
    default:
      return;
  }
  const uint8_t *inner = icmp + ICMP_HEADER_BYTES;
  size_t header = (inner[0] & IPV4_IHL_MASK) * IPV4_WORD_BYTES;
  if (inner[0] >> IPV4_VERSION_SHIFT != IPV4_VERSION || header < IPV4_HEADER_MIN ||
      total < outer + ICMP_HEADER_BYTES + header + UDP_HEADER_BYTES ||
      read_u16(inner + IPV4_LENGTH_OFFSET) < header + UDP_HEADER_BYTES ||
      inner[IPV4_PROTOCOL_OFFSET] != IPPROTO_UDP ||
      (read_u16(inner + IPV4_FRAGMENT_OFFSET) & IPV4_FRAGMENT_MASK) != 0 ||
      memcmp(inner + IPV4_TARGET_OFFSET, &batch->target_ip, sizeof(batch->target_ip)) != 0 ||
      memcmp(inner + IPV4_SOURCE_OFFSET, ip + IPV4_TARGET_OFFSET, sizeof(uint32_t)) != 0)
    return;
  const uint8_t *udp = inner + header;
  for (uint8_t i = 0; i < batch->count; ++i) {
    wifi_udp_scan_port_t *port = &batch->ports[i];
    if (!port->is_done && port->attempts != 0 && read_u16(udp) == port->local_port &&
        read_u16(udp + UDP_TARGET_PORT_OFFSET) == port->port &&
        read_u16(udp + UDP_LENGTH_OFFSET) == UDP_HEADER_BYTES + port->payload_size &&
        read_u16(inner + IPV4_LENGTH_OFFSET) == header + UDP_HEADER_BYTES + port->payload_size &&
        (port->local_ip == INADDR_ANY ||
         memcmp(inner + IPV4_SOURCE_OFFSET, &port->local_ip, sizeof(port->local_ip)) == 0)) {
      port->is_done = true;
      if (icmp[1] != ICMP_PORT_UNREACHABLE) {
        batch->result =
            (wifi_udp_scan_result_t){.port = port->port, .status = WIFI_UDP_SCAN_FILTERED};
        request->on_result(&batch->result, request->context);
      }
    }
  }
}
