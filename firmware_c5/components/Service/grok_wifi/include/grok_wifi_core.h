// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GROK_WIFI_CORE_H
#define GROK_WIFI_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GROK_PROFILE_MAX 4096
#define GROK_CA_MAX      3072
#define GROK_TOKEN_MAX   128
#define GROK_SERVER_NAME "highboy-grok.local"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  char host[16];
  uint16_t port;
  char token[GROK_TOKEN_MAX + 1];
  char ca_pem[GROK_CA_MAX + 1];
  char tls_server_name[32];
} grok_wifi_profile_t;

// Parse the complete, NUL-terminated JSON. No DNS, permissive certificate
// modes, public Internet addresses or embedded NULs are accepted.
bool grok_wifi_profile_parse(const char *json, size_t length, grok_wifi_profile_t *out);

typedef struct {
  uint8_t *data;
  size_t capacity, head, used;
} grok_wifi_ring_t;

// All accesses require the caller's lock; these functions never allocate.
size_t grok_wifi_ring_free(const grok_wifi_ring_t *ring);
bool grok_wifi_ring_write(grok_wifi_ring_t *ring, const uint8_t *data, size_t size);
size_t grok_wifi_ring_peek(const grok_wifi_ring_t *ring, uint8_t *out, size_t size);
void grok_wifi_ring_drop(grok_wifi_ring_t *ring, size_t size);

#ifdef __cplusplus
}
#endif

#endif
