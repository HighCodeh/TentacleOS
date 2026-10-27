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

#include "grok_wifi_core.h"
#include <string.h>
#include "cJSON.h"

static bool private_ipv4(const char *ip) {
  unsigned parts[4] = {0};
  const char *p = ip;
  for (unsigned i = 0; i < 4; i++) {
    unsigned digits = 0;
    while (*p >= '0' && *p <= '9') {
      if (++digits > 3)
        return false;
      parts[i] = parts[i] * 10 + (unsigned)(*p++ - '0');
    }
    if (!digits || parts[i] > 255 || (digits > 1 && p[-(int)digits] == '0'))
      return false;
    if (i < 3) {
      if (*p++ != '.')
        return false;
    } else if (*p)
      return false;
  }
  return parts[0] == 10 || parts[0] == 127 ||
         (parts[0] == 172 && parts[1] >= 16 && parts[1] <= 31) ||
         (parts[0] == 192 && parts[1] == 168);
}

static bool text_field(const cJSON *item, char *out, size_t cap) {
  if (!cJSON_IsString(item) || !item->valuestring)
    return false;
  size_t n = strlen(item->valuestring);
  if (!n || n >= cap)
    return false;
  memcpy(out, item->valuestring, n + 1);
  return true;
}

bool grok_wifi_profile_parse(const char *json, size_t length, grok_wifi_profile_t *out) {
  if (!json || !out || !length || length > GROK_PROFILE_MAX || memchr(json, 0, length))
    return false;
  memset(out, 0, sizeof(*out));
  cJSON *root = cJSON_ParseWithLengthOpts(json, length + 1, NULL, true);
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return false;
  }
  const char *keys[] = {"host", "port", "token", "ca_pem", "tls_server_name", "version"};
  unsigned fields = 0;
  bool ok = true;
  for (const cJSON *it = root->child; it; it = it->next) {
    unsigned k;
    for (k = 0; k < 6; k++)
      if (it->string && !strcmp(it->string, keys[k]))
        break;
    if (k == 6 || (fields & (1u << k))) {
      ok = false;
      break;
    }
    fields |= 1u << k;
  }
  if ((fields & 31u) != 31u)
    ok = false;
  const cJSON *port = cJSON_GetObjectItemCaseSensitive(root, "port");
  const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
  ok = ok && cJSON_IsNumber(port) && port->valuedouble >= 1 && port->valuedouble <= 65535 &&
       port->valuedouble == (double)port->valueint;
  ok = ok && (!version || (cJSON_IsNumber(version) && version->valuedouble == 1));
  ok = ok &&
       text_field(cJSON_GetObjectItemCaseSensitive(root, "host"), out->host, sizeof(out->host));
  ok = ok && private_ipv4(out->host);
  ok = ok &&
       text_field(cJSON_GetObjectItemCaseSensitive(root, "token"), out->token, sizeof(out->token));
  ok = ok && strlen(out->token) >= 32;
  for (const char *p = out->token; ok && *p; p++) {
    ok = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
         *p == '-' || *p == '_';
  }
  ok = ok && text_field(cJSON_GetObjectItemCaseSensitive(root, "ca_pem"),
                        out->ca_pem,
                        sizeof(out->ca_pem));
  ok = ok && !strncmp(out->ca_pem, "-----BEGIN CERTIFICATE-----", 27) &&
       strstr(out->ca_pem, "-----END CERTIFICATE-----") != NULL;
  ok = ok && text_field(cJSON_GetObjectItemCaseSensitive(root, "tls_server_name"),
                        out->tls_server_name,
                        sizeof(out->tls_server_name));
  ok = ok && !strcmp(out->tls_server_name, GROK_SERVER_NAME);
  if (ok)
    out->port = (uint16_t)port->valueint;
  // Wipe parser-owned credential copies before cJSON releases them.
  cJSON *token = cJSON_GetObjectItemCaseSensitive(root, "token");
  if (cJSON_IsString(token) && token->valuestring) {
    volatile char *p = token->valuestring;
    size_t n = strlen(token->valuestring);
    while (n--)
      *p++ = 0;
  }
  cJSON_Delete(root);
  if (!ok) {
    volatile uint8_t *p = (volatile uint8_t *)out;
    for (size_t i = 0; i < sizeof(*out); i++)
      p[i] = 0;
  }
  return ok;
}

size_t grok_wifi_ring_free(const grok_wifi_ring_t *r) {
  return r && r->data && r->used <= r->capacity ? r->capacity - r->used : 0;
}

bool grok_wifi_ring_write(grok_wifi_ring_t *r, const uint8_t *data, size_t n) {
  if (!r || !data || !r->data || !r->capacity || r->head >= r->capacity ||
      n > grok_wifi_ring_free(r))
    return false;
  size_t tail = (r->head + r->used) % r->capacity;
  size_t first = n < r->capacity - tail ? n : r->capacity - tail;
  memcpy(r->data + tail, data, first);
  memcpy(r->data, data + first, n - first);
  r->used += n;
  return true;
}

size_t grok_wifi_ring_peek(const grok_wifi_ring_t *r, uint8_t *out, size_t n) {
  if (!r || !out || !r->data || !r->capacity)
    return 0;
  if (n > r->used)
    n = r->used;
  size_t first = n < r->capacity - r->head ? n : r->capacity - r->head;
  memcpy(out, r->data + r->head, first);
  memcpy(out + first, r->data, n - first);
  return n;
}

void grok_wifi_ring_drop(grok_wifi_ring_t *r, size_t n) {
  if (!r || !r->data || !r->capacity || n > r->used)
    return;
  r->head = (r->head + n) % r->capacity;
  r->used -= n;
}
