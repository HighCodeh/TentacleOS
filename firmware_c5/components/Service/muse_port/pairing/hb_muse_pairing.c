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

#include "hb_muse_pairing.h"
#include "link_pairing.h"
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_log.h"
#define PAIR_TRACE(stage, ok) ESP_LOGI("MUSE_PAIR", "stage=%s ok=%u", stage, (unsigned)(ok))
#else
#define PAIR_TRACE(stage, ok) \
  do {                        \
    (void)(stage);            \
    (void)(ok);               \
  } while (0)
#endif

static hb_muse_pairing_config_t s_ops;
static hb_muse_pairing_status_t s_status;
static char s_node[128], s_device[128], s_mac[18], s_version[64];
static char s_token[HB_MUSE_PAIRING_TOKEN_CAP];
static uint64_t s_last_window, s_last_connection, s_ticket, s_edge, s_now, s_deadline;
static bool s_initialized, s_clock_started, s_clock_fault;
static void wipe(void *memory, size_t bytes) {
  volatile unsigned char *p = memory;
  while (bytes--)
    *p++ = 0;
}
static size_t length(const char *text, size_t cap) {
  if (!text)
    return cap;
  size_t n = 0;
  while (n < cap && text[n])
    ++n;
  return n;
}
static bool visible(const char *text, size_t cap, bool empty, bool spaces) {
  size_t n = length(text, cap);
  if (n == cap || (!n && !empty))
    return false;
  for (size_t i = 0; i < n; ++i) {
    unsigned char c = (unsigned char)text[i];
    if (c < (spaces ? 0x20u : 0x21u) || c > 0x7eu)
      return false;
  }
  return true;
}
static bool copy(char *out, size_t cap, const char *text) {
  size_t n = length(text, cap);
  if (n == cap)
    return false;
  memcpy(out, text, n + 1u);
  return true;
}
static void delete_secret_json(cJSON *json) {
  if (!json)
    return;
  for (cJSON *item = json->child; item; item = item->next)
    if (item->valuestring)
      wipe(item->valuestring, strlen(item->valuestring));
  cJSON_Delete(json);
}
static void secret_free(char *text, size_t bytes) {
  if (text)
    wipe(text, bytes);
  free(text);
}
static void fail(void) {
  link_pairing_reset();
  s_status.phase = HB_MUSE_PAIRING_FAILED;
  s_status.sdk_generation = 0;
  s_status.confirmation_ticket = 0;
  s_status.release_seen = false;
}
static bool clock_ok(uint64_t now) {
  if (s_clock_fault || (s_clock_started && now < s_now)) {
    s_clock_fault = true;
    fail();
    return false;
  }
  s_now = now;
  s_clock_started = true;
  return true;
}
static bool current(void *unused) {
  (void)unused;
  if (!s_initialized || !clock_ok(s_ops.now_ms(s_ops.user)))
    return false;
  bool phase = s_status.phase >= HB_MUSE_PAIRING_HELLO && s_status.phase <= HB_MUSE_PAIRING_STORED;
  return s_initialized && !s_clock_fault && phase && s_status.window && s_status.connection &&
         s_now < s_deadline && s_ops.current(s_ops.user, s_status.window, s_status.connection) &&
         s_status.sdk_generation && link_pairing_session_is_current(s_status.sdk_generation);
}
static bool provisioning_current(void *unused) {
  return current(unused) && link_pairing_provisioning_session_valid(s_status.sdk_generation);
}
static bool device_info(void) {
  cJSON *root = cJSON_CreateObject();
  bool ok = root && cJSON_AddStringToObject(root, "type", "device_info") &&
            cJSON_AddStringToObject(root, "node_id", s_node) &&
            cJSON_AddStringToObject(root, "version", s_version);
  if (ok)
    link_pairing_add_device_info(root);
  char *json = ok ? cJSON_PrintUnformatted(root) : NULL;
  cJSON_Delete(root);
  ok = json && s_ops.current(s_ops.user, s_status.window, s_status.connection) &&
       s_ops.send(s_ops.user, s_status.connection, json, strlen(json), 0) &&
       s_ops.current(s_ops.user, s_status.window, s_status.connection);
  free(json);
  return ok;
}
static bool send_json(const char *json, uint32_t record) {
  if (!json || !current(NULL) || (record && !link_pairing_record_session_is_current(record)))
    return false;
  size_t bytes = length(json, HB_MUSE_PAIRING_JSON_MAX + 1u);
  return bytes && bytes <= HB_MUSE_PAIRING_JSON_MAX &&
         s_ops.send(s_ops.user, s_status.connection, json, bytes, record) && current(NULL) &&
         (!record || link_pairing_record_session_is_current(record));
}
static bool emit_status(const char *status, bool include_token) {
  cJSON *root = cJSON_CreateObject();
  bool ok = root && cJSON_AddStringToObject(root, "type", "status") &&
            cJSON_AddStringToObject(root, "status", status) &&
            (!include_token || cJSON_AddStringToObject(root, "sdk_token", s_token));
  char *plain = ok ? cJSON_PrintUnformatted(root) : NULL;
  delete_secret_json(root);
  uint32_t record = 0;
  char *encrypted =
      plain ? link_pairing_encrypt_json(plain, s_status.sdk_generation, &record) : NULL;
  if (plain)
    secret_free(plain, strlen(plain));
  ok = encrypted && record && send_json(encrypted, record);
  if (encrypted)
    secret_free(encrypted, strlen(encrypted));
  return ok;
}
/* Bound parser work, reject duplicate keys, nested values and embedded NUL.
 * Unknown provisioning fields are checked separately. cJSON accepts a trailing
 * second JSON document unless require_null_terminated is requested explicitly. */
static cJSON *parse(const uint8_t *bytes, size_t n) {
  if (!bytes || !n || n > HB_MUSE_PAIRING_JSON_MAX || memchr(bytes, 0, n))
    return NULL;
  bool quoted = false;
  for (size_t i = 0; i < n; ++i) {
    if (bytes[i] == '"')
      quoted = !quoted;
    else if (quoted && bytes[i] == '\\') {
      if (++i >= n)
        return NULL;
      if (bytes[i] == 'u' && i + 4u < n && !memcmp(bytes + i + 1u, "0000", 4))
        return NULL;
    } else if (!quoted && (bytes[i] == '[' || (bytes[i] == '{' && i && memchr(bytes, '{', i))))
      return NULL;
  }
  char *text = calloc(1, n + 1u);
  if (!text)
    return NULL;
  memcpy(text, bytes, n);
  cJSON *root = cJSON_ParseWithLengthOpts(text, n + 1u, NULL, 1);
  secret_free(text, n);
  if (!cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return NULL;
  }
  size_t fields = 0;
  for (cJSON *item = root->child; item; item = item->next) {
    if (++fields > 16u || !item->string || cJSON_IsObject(item) || cJSON_IsArray(item)) {
      delete_secret_json(root);
      return NULL;
    }
    for (cJSON *prior = root->child; prior != item; prior = prior->next)
      if (!strcmp(prior->string, item->string)) {
        delete_secret_json(root);
        return NULL;
      }
  }
  return root;
}
static const char *string(cJSON *root, const char *key) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
  return cJSON_IsString(item) ? item->valuestring : NULL;
}
static bool optional_default(cJSON *root, const char *key, const char *value) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
  return !item || (cJSON_IsString(item) && item->valuestring &&
                   (!item->valuestring[0] || !strcmp(item->valuestring, value)));
}
static bool legacy_api_metadata(cJSON *root) {
  /* Upstream app.c routes current requests using api_url_v2, not api_url.
   * Accept bounded legacy metadata, but never copy/store/use its destination.
   * The active v2 API and Noise host remain pinned below. */
  cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "api_url");
  return !item || (cJSON_IsString(item) && visible(item->valuestring, 2048u, true, false));
}
static bool token_valid(const char *token) {
  if (!visible(token, HB_MUSE_PAIRING_TOKEN_CAP, false, false))
    return false;
  /* Bearer-token alphabet plus the SDK's hatch_refresh: prefix separator.
   * No JSON escapes/inflation or header whitespace is possible. */
  for (const unsigned char *p = (const unsigned char *)token; *p; ++p)
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
          strchr("-._~+/=:", *p)))
      return false;
  return true;
}
static bool refresh_valid(const char *token) {
  /* Opaque printable token, matching store/account validation. Reject all
   * whitespace, controls and non-ASCII; never normalize or truncate it. */
  if (!visible(token, HB_MUSE_PAIRING_REFRESH_CAP, false, false))
    return false;
  const char *raw = strrchr(token, ':');
  raw = raw ? raw + 1 : token;
  return *raw && strlen(raw) <= HB_MUSE_PAIRING_REFRESH_CAP - sizeof("Bearer hatch_refresh:");
}
static bool provision(cJSON *root) {
#define PROVISION_CHECK(stage, condition) \
  do {                                    \
    bool accepted = (condition);          \
    PAIR_TRACE(stage, accepted);          \
    if (!accepted)                        \
      return false;                       \
  } while (0)
  static const char *const keys[] = {"action",
                                     "ssid",
                                     "password",
                                     "access_token",
                                     "refresh_token",
                                     "username",
                                     "token_type",
                                     "api_url",
                                     "api_url_v2",
                                     "noise_host",
                                     "ota_url",
                                     "ota_force"};
  for (cJSON *item = root->child; item; item = item->next) {
    bool known = false;
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
      if (!strcmp(item->string, keys[i]))
        known = true;
    PROVISION_CHECK("provision_known_field", known);
  }
  const char *ssid = string(root, "ssid"), *password = string(root, "password");
  const char *access = string(root, "access_token"), *refresh = string(root, "refresh_token");
  const char *type = string(root, "token_type"), *username = string(root, "username");
  cJSON *force = cJSON_GetObjectItemCaseSensitive(root, "ota_force");
  PROVISION_CHECK("provision_token_type", type && !strcmp(type, "device"));
  PROVISION_CHECK("provision_ssid_format", visible(ssid, 33, false, true));
  PROVISION_CHECK("provision_password_format", visible(password, 65, false, true));
  PROVISION_CHECK("provision_access_format", token_valid(access));
  PROVISION_CHECK("provision_refresh_format", refresh_valid(refresh));
  PROVISION_CHECK("provision_username_format",
                  !cJSON_GetObjectItemCaseSensitive(root, "username") ||
                      visible(username, 128, true, true));
  PROVISION_CHECK("provision_api_v2_policy",
                  optional_default(root, "api_url_v2", "https://api.muse.ai"));
  PROVISION_CHECK("provision_legacy_api_ignored", legacy_api_metadata(root));
  PROVISION_CHECK("provision_noise_policy",
                  optional_default(root, "noise_host", "hatch.metaaivm.com"));
  PROVISION_CHECK("provision_ota_policy",
                  optional_default(root, "ota_url", "") && (!force || cJSON_IsFalse(force)));
  PROVISION_CHECK("provision_session",
                  current(NULL) && link_pairing_session_confirmed() && s_ops.store &&
                      s_ops.wifi_connect);
  hb_muse_pairing_credentials_t *credentials = calloc(1, sizeof(*credentials));
  PROVISION_CHECK("provision_allocate", credentials != NULL);
#undef PROVISION_CHECK
  bool ok = copy(credentials->ssid, sizeof(credentials->ssid), ssid) &&
            copy(credentials->password, sizeof(credentials->password), password) &&
            copy(credentials->access_token, sizeof(credentials->access_token), access) &&
            copy(credentials->refresh_token, sizeof(credentials->refresh_token), refresh) &&
            copy(credentials->username, sizeof(credentials->username), username ? username : "");
  /* Default host encoded as empty to avoid duplicating configuration policy. */
  if (ok)
    s_status.sdk_generation = link_pairing_mark_provisioning_active();
  PAIR_TRACE("provision_generation", ok && s_status.sdk_generation != 0);
  uint64_t started = s_ops.now_ms(s_ops.user);
  ok = ok && provisioning_current(NULL) && emit_status("wifi_connecting", false) &&
       provisioning_current(NULL);
  PAIR_TRACE("provision_notify_connecting", ok);
  if (ok) {
    ok = s_ops.wifi_connect(s_ops.user, credentials, provisioning_current, NULL) &&
         provisioning_current(NULL);
    PAIR_TRACE("provision_wifi_connect", ok);
  }
  uint64_t connected_at = s_ops.now_ms(s_ops.user);
  ok = ok && connected_at >= started && connected_at - started < 15000u &&
       emit_status("wifi_connected", false) && provisioning_current(NULL);
  PAIR_TRACE("provision_notify_connected", ok);
  if (ok) {
    ok = s_ops.store(s_ops.user, credentials, provisioning_current, NULL) &&
         provisioning_current(NULL);
    PAIR_TRACE("provision_store_account", ok);
  }
  /* Official PAIR_THEN_RESTART semantics: auth_ok acknowledges durable
   * phone-issued device credentials, NOT API authentication or VM readiness. */
  if (ok) {
    ok = emit_status("auth_ok", false) && provisioning_current(NULL);
    PAIR_TRACE("provision_notify_auth", ok);
  }
  wipe(credentials, sizeof(*credentials));
  free(credentials);
  if (ok)
    s_status.phase = HB_MUSE_PAIRING_STORED;
  return ok;
}
static bool scan_wifi(void) {
  if (!current(NULL) || !link_pairing_session_confirmed() || !s_ops.wifi_scan)
    return false;
  hb_muse_pairing_network_t *networks = calloc(HB_MUSE_PAIRING_NETWORK_MAX, sizeof(*networks));
  if (!networks)
    return false;
  size_t count = 0;
  uint64_t started = s_ops.now_ms(s_ops.user);
  bool ok =
      s_ops.wifi_scan(s_ops.user, networks, HB_MUSE_PAIRING_NETWORK_MAX, &count, current, NULL) &&
      current(NULL) && count <= HB_MUSE_PAIRING_NETWORK_MAX;
  uint64_t ended = s_ops.now_ms(s_ops.user);
  ok = ok && ended >= started && ended - started < HB_MUSE_PAIRING_SCAN_TIMEOUT_MS;
  PAIR_TRACE("scan_driver_and_deadline", ok);
  cJSON *root = ok ? cJSON_CreateObject() : NULL;
  cJSON *array = root ? cJSON_AddArrayToObject(root, "networks") : NULL;
  ok = ok && root && array && cJSON_AddStringToObject(root, "type", "wifi_scan_result");
  for (size_t i = 0; ok && i < count; ++i) {
    ok = visible(networks[i].ssid, sizeof(networks[i].ssid), false, true) &&
         networks[i].rssi >= -127 && networks[i].rssi <= 0;
    cJSON *entry = ok ? cJSON_CreateObject() : NULL;
    ok = ok && entry && cJSON_AddStringToObject(entry, "ssid", networks[i].ssid) &&
         cJSON_AddNumberToObject(entry, "rssi", networks[i].rssi) &&
         cJSON_AddBoolToObject(entry, "secure", networks[i].secure);
    if (ok)
      ok = cJSON_AddItemToArray(array, entry);
    if (!ok)
      cJSON_Delete(entry);
  }
  char *plain = ok ? cJSON_PrintUnformatted(root) : NULL;
  cJSON_Delete(root);
  wipe(networks, HB_MUSE_PAIRING_NETWORK_MAX * sizeof(*networks));
  free(networks);
  uint32_t record = 0;
  char *encrypted = plain && current(NULL)
                        ? link_pairing_encrypt_json(plain, s_status.sdk_generation, &record)
                        : NULL;
  if (plain)
    secret_free(plain, strlen(plain));
  ok = encrypted && record && send_json(encrypted, record);
  if (encrypted)
    secret_free(encrypted, strlen(encrypted));
  return ok;
}
bool hb_muse_pairing_init(const hb_muse_pairing_config_t *config) {
  if (s_initialized || !config || !config->now_ms || !config->current || !config->send ||
      !visible(config->node_id, sizeof(s_node), false, false) ||
      !visible(config->device_id, sizeof(s_device), false, false) ||
      !visible(config->mac, sizeof(s_mac), false, false) ||
      !visible(config->firmware_version, sizeof(s_version), false, false) ||
      !visible(config->sdk_token, sizeof(s_token), false, false) ||
      strncmp(config->sdk_token, "mgst_", 5) || strlen(config->sdk_token) <= 5u)
    return false;
  s_ops = *config;
  (void)copy(s_node, sizeof(s_node), config->node_id);
  (void)copy(s_device, sizeof(s_device), config->device_id);
  (void)copy(s_mac, sizeof(s_mac), config->mac);
  (void)copy(s_version, sizeof(s_version), config->firmware_version);
  (void)copy(s_token, sizeof(s_token), config->sdk_token);
  /* NULL deliberately avoids upstream encrypt_status's token-prefix log. */
  link_pairing_init(s_node, s_device, s_mac, s_version, NULL);
  cJSON *info = cJSON_CreateObject();
  if (info)
    link_pairing_add_device_info(info);
  const char *auth = string(info, "pairing_auth"), *policy = string(info, "pairing_policy");
  cJSON *version = cJSON_GetObjectItemCaseSensitive(info, "pairing_protocol");
  cJSON *epoch = cJSON_GetObjectItemCaseSensitive(info, "pairing_auth_epoch");
  bool ok = auth && !strcmp(auth, "none") && policy && !strcmp(policy, "confirm_press") &&
            cJSON_IsNumber(version) && version->valuedouble == 5.0 && cJSON_IsNumber(epoch) &&
            epoch->valuedouble == 0.0;
  cJSON_Delete(info);
  s_initialized = ok;
  if (!ok) {
    wipe(s_token, sizeof(s_token));
    link_pairing_reset();
  }
  return ok;
}
bool hb_muse_pairing_open(uint64_t window, uint64_t now_ms) {
  if (!s_initialized || !clock_ok(now_ms) || !window || window <= s_last_window ||
      now_ms > UINT64_MAX - HB_MUSE_PAIRING_WINDOW_MS)
    return false;
  hb_muse_pairing_revoke();
  s_last_window = window;
  s_deadline = now_ms + HB_MUSE_PAIRING_WINDOW_MS;
  s_status.window = window;
  s_status.phase = HB_MUSE_PAIRING_OPEN;
  return true;
}
bool hb_muse_pairing_connected(uint64_t connection, uint64_t now_ms) {
  if (!s_initialized || !clock_ok(now_ms) || s_status.phase != HB_MUSE_PAIRING_OPEN ||
      s_status.connection || !connection || connection <= s_last_connection ||
      now_ms >= s_deadline || !s_ops.current(s_ops.user, s_status.window, connection))
    return false;
  s_last_connection = connection;
  s_status.connection = connection;
  link_pairing_reset();
  return true;
}
bool hb_muse_pairing_receive(uint64_t connection,
                             const uint8_t *json,
                             size_t bytes,
                             uint64_t now_ms) {
  if (!s_initialized || !clock_ok(now_ms) || !connection || connection != s_status.connection ||
      now_ms >= s_deadline || !s_ops.current(s_ops.user, s_status.window, connection))
    return false;
  cJSON *root = parse(json, bytes);
  if (!root) {
    fail();
    return false;
  }
  const char *action = string(root, "action");
  bool ok = false;
  const char *stage = "unrecognized_or_wrong_phase";
  if (action && !strcmp(action, "get_device_info") && s_status.phase == HB_MUSE_PAIRING_OPEN &&
      root->child && !root->child->next) {
    stage = "device_info";
    ok = device_info();
  } else if (action && !strcmp(action, "pairing_client_hello") &&
             s_status.phase == HB_MUSE_PAIRING_OPEN) {
    stage = "client_hello";
    char *reply = NULL;
    const char *error = link_pairing_handle_client_hello(root, &reply);
    s_status.sdk_generation = link_pairing_session_generation();
    s_status.phase = HB_MUSE_PAIRING_HELLO;
    ok = !error && reply && send_json(reply, 0);
    if (reply)
      secret_free(reply, strlen(reply));
  } else if (action && !strcmp(action, "pairing_encrypted") && current(NULL)) {
    stage = "encrypted_shape_or_decrypt";
    const char *cipher = string(root, "ciphertext");
    size_t encoded = length(cipher, 4097), plain_bytes = encoded * 3u / 4u;
    char *plain = NULL;
    bool shape = encoded && encoded <= 4096u && encoded % 4u != 1u;
    /* Pinned SDK accepts ONLY unpadded base64url, not '=' or whitespace. */
    for (size_t i = 0; shape && i < encoded; ++i) {
      unsigned char c = (unsigned char)cipher[i];
      shape = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_';
    }
    if (shape && !link_pairing_decrypt_command(root, &plain) && plain &&
        !memchr(plain, 0, plain_bytes)) {
      cJSON *command = parse((const uint8_t *)plain, plain_bytes);
      const char *inner = string(command, "action");
      if (inner && !strcmp(inner, "pairing_client_finished") &&
          s_status.phase == HB_MUSE_PAIRING_HELLO && command->child && !command->child->next) {
        stage = "client_finished";
        s_status.sdk_generation = link_pairing_handle_client_finished();
        if (s_status.sdk_generation && s_ticket != UINT64_MAX &&
            emit_status("confirm_required", false) &&
            link_pairing_arm_confirmation(s_status.sdk_generation) && current(NULL)) {
          s_status.confirmation_ticket = ++s_ticket;
          s_status.release_seen = false;
          s_status.phase = HB_MUSE_PAIRING_CONFIRM;
          ok = true;
        }
      } else if (inner && !strcmp(inner, "provision_v2") &&
                 s_status.phase == HB_MUSE_PAIRING_CONFIRMED) {
        stage = "provision";
        ok = provision(command);
      } else if (inner && !strcmp(inner, "wifi_scan") &&
                 s_status.phase == HB_MUSE_PAIRING_CONFIRMED && command->child &&
                 !command->child->next) {
        stage = "wifi_scan_response";
        ok = scan_wifi();
      }
      delete_secret_json(command);
    }
    if (plain)
      secret_free(plain, plain_bytes);
  }
  delete_secret_json(root);
  PAIR_TRACE(stage, ok);
  if (!ok)
    fail();
  return ok;
}
bool hb_muse_pairing_button(uint64_t ticket, uint64_t edge_serial, bool pressed, uint64_t now_ms) {
  if (!s_initialized || !clock_ok(now_ms) || s_status.phase != HB_MUSE_PAIRING_CONFIRM || !ticket ||
      ticket != s_status.confirmation_ticket || !edge_serial || edge_serial <= s_edge ||
      !current(NULL) || !link_pairing_confirmation_required())
    return false;
  s_edge = edge_serial;
  if (!pressed) {
    s_status.release_seen = true;
    return true;
  }
  if (!s_status.release_seen)
    return false;
  s_status.release_seen = false;
  s_status.sdk_generation = link_pairing_confirm_active_session();
  if (!s_status.sdk_generation || !current(NULL) || !emit_status("pairing_confirmed", true)) {
    fail();
    return false;
  }
  s_status.phase = HB_MUSE_PAIRING_CONFIRMED;
  s_status.confirmation_ticket = 0;
  return true;
}
void hb_muse_pairing_poll(uint64_t now_ms) {
  if (!s_initialized || !clock_ok(now_ms))
    return;
  if (s_status.phase == HB_MUSE_PAIRING_OFF || s_status.phase == HB_MUSE_PAIRING_FAILED)
    return;
  if (now_ms >= s_deadline ||
      (s_status.connection && !s_ops.current(s_ops.user, s_status.window, s_status.connection))) {
    fail();
    return;
  }
  if (s_status.phase >= HB_MUSE_PAIRING_HELLO &&
      (!current(NULL) ||
       (s_status.phase == HB_MUSE_PAIRING_CONFIRM && !link_pairing_confirmation_required()) ||
       (s_status.phase >= HB_MUSE_PAIRING_CONFIRMED && !link_pairing_session_confirmed())))
    fail();
}
void hb_muse_pairing_disconnect(uint64_t connection) {
  if (!s_initialized || !connection || connection != s_status.connection)
    return;
  if (s_status.phase == HB_MUSE_PAIRING_STORED) {
    hb_muse_pairing_revoke();
    return;
  }
  link_pairing_reset();
  s_status.connection = s_status.confirmation_ticket = 0;
  s_status.sdk_generation = 0;
  s_status.release_seen = false;
  s_status.phase = HB_MUSE_PAIRING_OPEN;
}
void hb_muse_pairing_revoke(void) {
  if (!s_initialized)
    return;
  link_pairing_reset();
  memset(&s_status, 0, sizeof(s_status));
  s_deadline = 0;
}
hb_muse_pairing_status_t hb_muse_pairing_status(void) {
  return s_status;
}
