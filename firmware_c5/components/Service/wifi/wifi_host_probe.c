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

#include "wifi_host_probe.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "lwip/inet_chksum.h"
#include "lwip/sockets.h"

#define PROBE_ATTEMPTS        2
#define PROBE_TIMEOUT_US      400000
#define PROBE_POLL_US         50000
#define PROBE_PACKET_BYTES    16
#define PROBE_REPLY_BYTES     128
#define IPV4_MIN_HEADER_BYTES 20
#define IPV4_VERSION          4
#define IPV4_VERSION_SHIFT    4
#define IPV4_HEADER_MASK      15
#define IPV4_WORD_BYTES       4
#define IPV4_PROTOCOL_OFFSET  9
#define IPV4_LENGTH_OFFSET    2
#define ICMP_ECHO_REQUEST     8
#define ICMP_ECHO_REPLY       0
#define ICMP_CHECKSUM_OFFSET  2
#define ICMP_ID_OFFSET        4
#define ICMP_SEQUENCE_OFFSET  6
#define ICMP_PAYLOAD_OFFSET   8
#define BYTE_BITS             8

typedef struct {
  int32_t socket_fd;
  uint32_t first_ip;
  uint8_t count;
  uint8_t attempts[WIFI_HOST_PROBE_MAX_HOSTS];
  int64_t deadlines[WIFI_HOST_PROBE_MAX_HOSTS];
  uint16_t reachable;
  uint16_t completed;
  uint8_t request[PROBE_PACKET_BYTES];
} wifi_host_probe_batch_t;

static const char *TAG = "WIFI_HOST_PROBE";
static atomic_bool s_is_cancelled;

static bool
matches_reply(const uint8_t *reply, size_t length, const uint8_t *request, uint16_t *out_sequence);
static esp_err_t receive_reply(wifi_host_probe_batch_t *batch, int32_t flags);

void wifi_host_probe_cancel(void) {
  atomic_store(&s_is_cancelled, true);
}

void wifi_host_probe_reset(void) {
  atomic_store(&s_is_cancelled, false);
}

esp_err_t wifi_host_probe_run(const char *ip, bool *out_reachable) {
  if (ip == NULL || out_reachable == NULL)
    return ESP_ERR_INVALID_ARG;
  *out_reachable = false;
  struct in_addr address;
  if (inet_pton(AF_INET, ip, &address) != 1)
    return ESP_ERR_INVALID_ARG;
  uint16_t reachable = 0;
  esp_err_t error = wifi_host_probe_run_batch(address.s_addr, 1, &reachable);
  *out_reachable = reachable != 0;
  return error;
}

esp_err_t wifi_host_probe_run_batch(uint32_t first_ip, uint8_t count, uint16_t *out_reachable) {
  if (out_reachable == NULL)
    return ESP_ERR_INVALID_ARG;
  *out_reachable = 0;
  uint32_t first = ntohl(first_ip);
  if (count == 0 || count > WIFI_HOST_PROBE_MAX_HOSTS || first > UINT32_MAX - (count - 1U))
    return ESP_ERR_INVALID_ARG;
  if (atomic_load(&s_is_cancelled))
    return ESP_ERR_INVALID_STATE;
  wifi_host_probe_batch_t batch = {.first_ip = first, .count = count};
  batch.socket_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (batch.socket_fd < 0) {
    ESP_LOGE(TAG, "Cannot create ICMP socket");
    return ESP_FAIL;
  }
  esp_err_t error = ESP_FAIL;
  struct timeval timeout = {.tv_usec = PROBE_POLL_US};
  if (setsockopt(batch.socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0)
    goto cleanup;
  batch.request[0] = ICMP_ECHO_REQUEST;
  uint16_t identifier = (uint16_t)esp_random();
  memcpy(batch.request + ICMP_ID_OFFSET, &identifier, sizeof(identifier));
  for (size_t i = ICMP_PAYLOAD_OFFSET; i < sizeof(batch.request); i += sizeof(uint32_t)) {
    uint32_t nonce = esp_random();
    memcpy(batch.request + i, &nonce, sizeof(nonce));
  }
  const uint16_t all_hosts = (1U << count) - 1U;
  while (batch.completed != all_hosts && !atomic_load(&s_is_cancelled)) {
    for (uint8_t i = 0; i < count && !atomic_load(&s_is_cancelled); ++i) {
      uint16_t bit = 1U << i;
      if ((batch.completed & bit) != 0 || esp_timer_get_time() < batch.deadlines[i])
        continue;
      if (batch.attempts[i] == PROBE_ATTEMPTS) {
        batch.completed |= bit;
        continue;
      }
      uint16_t sequence = htons(batch.attempts[i] * WIFI_HOST_PROBE_MAX_HOSTS + i);
      memcpy(batch.request + ICMP_SEQUENCE_OFFSET, &sequence, sizeof(sequence));
      memset(batch.request + ICMP_CHECKSUM_OFFSET, 0, sizeof(uint16_t));
      uint16_t checksum = inet_chksum(batch.request, sizeof(batch.request));
      memcpy(batch.request + ICMP_CHECKSUM_OFFSET, &checksum, sizeof(checksum));
      struct sockaddr_in target = {.sin_family = AF_INET};
      target.sin_addr.s_addr = htonl(first + i);
      if (sendto(batch.socket_fd,
                 batch.request,
                 sizeof(batch.request),
                 0,
                 (struct sockaddr *)&target,
                 sizeof(target)) != sizeof(batch.request)) {
        error = ESP_FAIL;
        goto cleanup;
      }
      ++batch.attempts[i];
      batch.deadlines[i] = esp_timer_get_time() + PROBE_TIMEOUT_US;
      // Drain immediate replies between sends instead of filling lwIP's raw mailbox.
      error = receive_reply(&batch, MSG_DONTWAIT);
      if (error != ESP_OK)
        goto cleanup;
    }
    if (batch.completed == all_hosts || atomic_load(&s_is_cancelled))
      break;
    error = receive_reply(&batch, 0);
    if (error != ESP_OK)
      goto cleanup;
  }
  error = atomic_load(&s_is_cancelled) ? ESP_ERR_INVALID_STATE : ESP_OK;
cleanup:
  *out_reachable = batch.reachable;
  close(batch.socket_fd);
  if (error != ESP_OK && error != ESP_ERR_INVALID_STATE)
    ESP_LOGE(TAG, "ICMP probe failed");
  return error;
}

static esp_err_t receive_reply(wifi_host_probe_batch_t *batch, int32_t flags) {
  uint8_t reply[PROBE_REPLY_BYTES];
  struct sockaddr_in source = {0};
  socklen_t source_size = sizeof(source);
  int32_t received = recvfrom(
      batch->socket_fd, reply, sizeof(reply), flags, (struct sockaddr *)&source, &source_size);
  if (received < 0)
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? ESP_OK : ESP_FAIL;
  uint32_t ip = ntohl(source.sin_addr.s_addr);
  if (source_size < sizeof(source) || source.sin_family != AF_INET || ip < batch->first_ip ||
      ip - batch->first_ip >= batch->count)
    return ESP_OK;
  uint8_t index = ip - batch->first_ip;
  uint16_t bit = 1U << index, sequence;
  if ((batch->completed & bit) == 0 && matches_reply(reply, received, batch->request, &sequence) &&
      sequence % WIFI_HOST_PROBE_MAX_HOSTS == index &&
      sequence / WIFI_HOST_PROBE_MAX_HOSTS < batch->attempts[index]) {
    batch->reachable |= bit;
    batch->completed |= bit;
  }
  return ESP_OK;
}

static bool
matches_reply(const uint8_t *reply, size_t length, const uint8_t *request, uint16_t *out_sequence) {
  if (length < IPV4_MIN_HEADER_BYTES + PROBE_PACKET_BYTES ||
      (reply[0] >> IPV4_VERSION_SHIFT) != IPV4_VERSION ||
      reply[IPV4_PROTOCOL_OFFSET] != IPPROTO_ICMP)
    return false;
  size_t header_bytes = (reply[0] & IPV4_HEADER_MASK) * IPV4_WORD_BYTES;
  size_t packet_bytes =
      ((size_t)reply[IPV4_LENGTH_OFFSET] << BYTE_BITS) | reply[IPV4_LENGTH_OFFSET + 1];
  if (header_bytes < IPV4_MIN_HEADER_BYTES || packet_bytes > length ||
      packet_bytes != header_bytes + PROBE_PACKET_BYTES)
    return false;
  const uint8_t *echo = reply + header_bytes;
  if (echo[0] != ICMP_ECHO_REPLY || echo[1] != 0 || inet_chksum(echo, PROBE_PACKET_BYTES) != 0 ||
      memcmp(echo + ICMP_ID_OFFSET, request + ICMP_ID_OFFSET, sizeof(uint16_t)) != 0 ||
      memcmp(echo + ICMP_PAYLOAD_OFFSET,
             request + ICMP_PAYLOAD_OFFSET,
             PROBE_PACKET_BYTES - ICMP_PAYLOAD_OFFSET) != 0)
    return false;
  *out_sequence =
      ((uint16_t)echo[ICMP_SEQUENCE_OFFSET] << BYTE_BITS) | echo[ICMP_SEQUENCE_OFFSET + 1];
  return true;
}
