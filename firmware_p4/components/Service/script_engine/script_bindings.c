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

#include "script_bindings.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "esp_log.h"

#define SCRIPT_MAC_BYTES          6
#define SCRIPT_MAC_STRIDE         3
#define SCRIPT_HEX_DIGITS_MAX     8
#define SCRIPT_CAPS_PREFIX_LENGTH 9
#define SCRIPT_HEX_PREFIX_LENGTH  2
#define SCRIPT_HEX_LETTER_OFFSET  10
#define REQUIRE_JSON(value) \
  do {                      \
    if (!(value))           \
      goto no_memory;       \
  } while (0)
#define SCRIPT_MAC_TEXT                 18
#define SCRIPT_HEX_RADIX                16
#define SCRIPT_DELAY_MAX_MS             30000
#define SCRIPT_RF_ABI_MINOR             10
#define SCRIPT_IR_ABI_MINOR             11
#define SCRIPT_RADIO_VIEW_ABI_MINOR     12
#define SCRIPT_SUBGHZ_ABI_MINOR         13
#define SCRIPT_LORA_TX_ABI_MINOR        14
#define SCRIPT_SPECTRUM_ABI_MINOR       15
#define SCRIPT_IR_EXT_ABI_MINOR         16
#define SCRIPT_NFC_BLOCK_ABI_MINOR      17
#define SCRIPT_SUBGHZ_RAW_ABI_MINOR     18
#define SCRIPT_SUBGHZ_REPLAY_ABI_MINOR  19
#define SCRIPT_LORA_NODES_ABI_MINOR     20
#define SCRIPT_SUBGHZ_SESSION_ABI_MINOR 21
#define SCRIPT_NFC_SESSION_ABI_MINOR    22
#define SCRIPT_LORA_SESSION_ABI_MINOR   23
#define SCRIPT_LORA_DATA_MAX            UINT8_MAX
#define SCRIPT_LORA_POWER_MIN_DBM       (-9)
#define SCRIPT_LORA_POWER_MAX_DBM       22
#define SCRIPT_NFC_UID_BYTES            4
#define SCRIPT_NFC_KEY_BYTES            6
#define SCRIPT_NFC_BLOCK_BYTES          16
#define SCRIPT_BYTE_HEX_DIGITS          2
#define SCRIPT_SUBGHZ_PULSES_MAX        128
#define SCRIPT_SUBGHZ_PULSE_MAX_US      32767
#define SCRIPT_DECIMAL_BASE             10

static const char *TAG = "SCRIPT_BINDINGS";

typedef enum {
  SCRIPT_BINDINGS_VERSION,
  SCRIPT_BINDINGS_SHOULD_STOP,
  SCRIPT_BINDINGS_RESOURCE_LOST,
  SCRIPT_BINDINGS_YIELD,
  SCRIPT_BINDINGS_DELAY_MS,
  SCRIPT_BINDINGS_LOG,
  SCRIPT_BINDINGS_LED_SET,
  SCRIPT_BINDINGS_WIFI_SCAN,
  SCRIPT_BINDINGS_WIFI_AP_COUNT,
  SCRIPT_BINDINGS_WIFI_IS_CONNECTED,
  SCRIPT_BINDINGS_WIFI_CONNECTED_SSID,
  SCRIPT_BINDINGS_WIFI_DISCONNECT,
  SCRIPT_BINDINGS_WIFI_DEAUTH_DETECT,
  SCRIPT_BINDINGS_WIFI_PROBE_MONITOR,
  SCRIPT_BINDINGS_WIFI_AP_GET,
  SCRIPT_BINDINGS_WIFI_CONNECT,
  SCRIPT_BINDINGS_WIFI_SNIFFER_START,
  SCRIPT_BINDINGS_WIFI_SIGNAL_MONITOR,
  SCRIPT_BINDINGS_WIFI_DEAUTH,
  SCRIPT_BINDINGS_WIFI_DEAUTH_BROADCAST,
  SCRIPT_BINDINGS_WIFI_FLOOD,
  SCRIPT_BINDINGS_WIFI_BEACON_SPAM,
  SCRIPT_BINDINGS_WIFI_EVIL_TWIN,
  SCRIPT_BINDINGS_WIFI_SESSION_STOP,
  SCRIPT_BINDINGS_WIFI_CHANNEL_HOP,
  SCRIPT_BINDINGS_BLE_DEVICE_COUNT,
  SCRIPT_BINDINGS_BLE_DISCONNECT,
  SCRIPT_BINDINGS_BLE_SET_RANDOM_MAC,
  SCRIPT_BINDINGS_BLE_ADV_START,
  SCRIPT_BINDINGS_BLE_ADV_STOP,
  SCRIPT_BINDINGS_BLE_SNIFFER_START,
  SCRIPT_BINDINGS_BLE_SKIMMER_DETECT,
  SCRIPT_BINDINGS_BLE_TRACKER_DETECT,
  SCRIPT_BINDINGS_BLE_HID_START,
  SCRIPT_BINDINGS_BLE_HID_STOP,
  SCRIPT_BINDINGS_BLE_HID_IS_CONNECTED,
  SCRIPT_BINDINGS_BLE_SCAN,
  SCRIPT_BINDINGS_BLE_DEVICE_GET,
  SCRIPT_BINDINGS_BLE_CONNECT,
  SCRIPT_BINDINGS_BLE_SPAM,
  SCRIPT_BINDINGS_BLE_FLOOD,
  SCRIPT_BINDINGS_BLE_SESSION_STOP,
  SCRIPT_BINDINGS_BLE_HID_SEND_KEY,
  SCRIPT_BINDINGS_UI_SCREEN_OPEN,
  SCRIPT_BINDINGS_UI_SCREEN_CLOSE,
  SCRIPT_BINDINGS_UI_METRICS,
  SCRIPT_BINDINGS_UI_LABEL,
  SCRIPT_BINDINGS_UI_RECT,
  SCRIPT_BINDINGS_UI_BAR,
  SCRIPT_BINDINGS_UI_SET_TEXT,
  SCRIPT_BINDINGS_UI_SET_VALUE,
  SCRIPT_BINDINGS_UI_SET_POS,
  SCRIPT_BINDINGS_UI_SET_SIZE,
  SCRIPT_BINDINGS_UI_SET_TEXT_COLOR,
  SCRIPT_BINDINGS_UI_SET_BG_COLOR,
  SCRIPT_BINDINGS_UI_SET_HIDDEN,
  SCRIPT_BINDINGS_UI_DEL,
  SCRIPT_BINDINGS_UI_POLL_EVENT,
  SCRIPT_BINDINGS_UI_CANVAS_END,
  SCRIPT_BINDINGS_FS_SIZE,
  SCRIPT_BINDINGS_FS_LIST,
  SCRIPT_BINDINGS_FS_OPEN,
  SCRIPT_BINDINGS_FS_READ,
  SCRIPT_BINDINGS_FS_SEEK,
  SCRIPT_BINDINGS_FS_CLOSE,
  SCRIPT_BINDINGS_NFC_POLL,
  SCRIPT_BINDINGS_RFID_READ,
  SCRIPT_BINDINGS_IR_RECEIVE,
  SCRIPT_BINDINGS_IR_SEND,
  SCRIPT_BINDINGS_SUBGHZ_POLL,
  SCRIPT_BINDINGS_LORA_MESH_POLL,
  SCRIPT_BINDINGS_SUBGHZ_RECEIVE,
  SCRIPT_BINDINGS_SUBGHZ_TX_RAW,
  SCRIPT_BINDINGS_LORA_MESH_SEND_TEXT,
  SCRIPT_BINDINGS_SUBGHZ_SPECTRUM_CAPTURE,
  SCRIPT_BINDINGS_IR_GET_RAW,
  SCRIPT_BINDINGS_IR_SEND_RAW,
  SCRIPT_BINDINGS_IR_SEND_FILE,
  SCRIPT_BINDINGS_IR_SEND_AC,
  SCRIPT_BINDINGS_NFC_READ_BLOCK,
  SCRIPT_BINDINGS_NFC_WRITE_BLOCK,
  SCRIPT_BINDINGS_SUBGHZ_GET_RAW,
  SCRIPT_BINDINGS_SUBGHZ_REPLAY,
  SCRIPT_BINDINGS_LORA_NODE_COUNT,
  SCRIPT_BINDINGS_LORA_NODE_GET,
  SCRIPT_BINDINGS_SUBGHZ_RX_START,
  SCRIPT_BINDINGS_SUBGHZ_SPECTRUM_START,
  SCRIPT_BINDINGS_SUBGHZ_SPECTRUM_POLL,
  SCRIPT_BINDINGS_SUBGHZ_SET_FREQUENCY,
  SCRIPT_BINDINGS_SUBGHZ_SESSION_STOP,
  SCRIPT_BINDINGS_NFC_EMULATE_START,
  SCRIPT_BINDINGS_NFC_SESSION_STOP,
  SCRIPT_BINDINGS_LORA_CONFIGURE,
  SCRIPT_BINDINGS_LORA_SEND,
  SCRIPT_BINDINGS_LORA_RX_START,
  SCRIPT_BINDINGS_LORA_RX_POLL,
  SCRIPT_BINDINGS_LORA_MESH_START,
  SCRIPT_BINDINGS_LORA_SESSION_STOP,
  SCRIPT_BINDINGS_METHOD_COUNT
} script_bindings_method_id_t;

typedef struct {
  const char *name;
  const char *signature;
  uint32_t caps;
} script_bindings_method_t;

static const script_bindings_method_t METHODS[] = {
    {"version", "", 0},
    {"should_stop", "", 0},
    {"resource_lost", "", 0},
    {"yield", "", 0},
    {"delay_ms", "u", 0},
    {"log", "s", 0},
    {"led_set", "yyy", TOS_CAP_UI},
    {"wifi.scan", "", TOS_CAP_RADIO_RX},
    {"wifi.ap_count", "", TOS_CAP_RADIO_RX},
    {"wifi.is_connected", "", 0},
    {"wifi.connected_ssid", "", 0},
    {"wifi.disconnect", "", TOS_CAP_RADIO_TX},
    {"wifi.deauth_detect", "", TOS_CAP_RADIO_RX},
    {"wifi.probe_monitor", "", TOS_CAP_RADIO_RX},
    {"wifi.ap_get", "i", TOS_CAP_RADIO_RX},
    {"wifi.connect", "ss", TOS_CAP_RADIO_TX},
    {"wifi.sniffer_start", "yyb", TOS_CAP_RADIO_RX},
    {"wifi.signal_monitor", "my", TOS_CAP_RADIO_RX},
    {"wifi.deauth", "mmyy", TOS_CAP_RADIO_TX},
    {"wifi.deauth_broadcast", "myy", TOS_CAP_RADIO_TX},
    {"wifi.flood", "ymy", TOS_CAP_RADIO_TX},
    {"wifi.beacon_spam", "S", TOS_CAP_RADIO_TX},
    {"wifi.evil_twin", "s", TOS_CAP_RADIO_TX},
    {"wifi.session_stop", "u", 0},
    {"wifi.channel_hop", "b", TOS_CAP_RADIO_TX},
    {"ble.device_count", "", TOS_CAP_RADIO_RX},
    {"ble.disconnect", "", TOS_CAP_RADIO_TX},
    {"ble.set_random_mac", "", TOS_CAP_RADIO_TX},
    {"ble.adv_start", "", TOS_CAP_RADIO_TX},
    {"ble.adv_stop", "", TOS_CAP_RADIO_TX},
    {"ble.sniffer_start", "", TOS_CAP_RADIO_RX},
    {"ble.skimmer_detect", "", TOS_CAP_RADIO_RX},
    {"ble.tracker_detect", "", TOS_CAP_RADIO_RX},
    {"ble.hid_start", "", TOS_CAP_HID},
    {"ble.hid_stop", "", TOS_CAP_HID},
    {"ble.hid_is_connected", "", TOS_CAP_HID},
    {"ble.scan", "u", TOS_CAP_RADIO_RX},
    {"ble.device_get", "i", TOS_CAP_RADIO_RX},
    {"ble.connect", "my", TOS_CAP_RADIO_TX},
    {"ble.spam", "y", TOS_CAP_RADIO_TX},
    {"ble.flood", "my", TOS_CAP_RADIO_TX},
    {"ble.session_stop", "u", 0},
    {"ble.hid_send_key", "yy", TOS_CAP_HID},
    {"ui.screen_open", "", TOS_CAP_UI},
    {"ui.screen_close", "", TOS_CAP_UI},
    {"ui.metrics", "", TOS_CAP_UI},
    {"ui.label", "uiis", TOS_CAP_UI},
    {"ui.rect", "uiiii", TOS_CAP_UI},
    {"ui.bar", "uiiiiii", TOS_CAP_UI},
    {"ui.set_text", "us", TOS_CAP_UI},
    {"ui.set_value", "ui", TOS_CAP_UI},
    {"ui.set_pos", "uii", TOS_CAP_UI},
    {"ui.set_size", "uii", TOS_CAP_UI},
    {"ui.set_text_color", "uu", TOS_CAP_UI},
    {"ui.set_bg_color", "uu", TOS_CAP_UI},
    {"ui.set_hidden", "ub", TOS_CAP_UI},
    {"ui.del", "u", TOS_CAP_UI},
    {"ui.poll_event", "", TOS_CAP_UI},
    {"ui.canvas_end", "", TOS_CAP_UI},
    {"fs.size", "s", TOS_CAP_FS_READ},
    {"fs.list", "su", TOS_CAP_FS_READ},
    {"fs.open", "s", TOS_CAP_FS_READ},
    {"fs.read", "uu", TOS_CAP_FS_READ},
    {"fs.seek", "uu", TOS_CAP_FS_READ},
    {"fs.close", "u", 0},
    {"nfc.poll", "u", TOS_CAP_RADIO_RX},
    {"rfid.read", "u", TOS_CAP_RADIO_RX},
    {"ir.receive", "u", TOS_CAP_RADIO_RX},
    {"ir.send", "yuub", TOS_CAP_RADIO_TX},
    {"subghz.rx_poll", "", TOS_CAP_RADIO_RX},
    {"lora.mesh_poll", "", TOS_CAP_RADIO_RX},
    {"subghz.receive", "uyu", TOS_CAP_RADIO_RX},
    {"subghz.tx_raw", "us", TOS_CAP_RADIO_TX},
    {"lora.mesh_send_text", "s", TOS_CAP_RADIO_TX},
    {"subghz.spectrum_capture", "uu", TOS_CAP_RADIO_RX},
    {"ir.get_last_raw", "u", TOS_CAP_RADIO_RX},
    {"ir.send_raw", "us", TOS_CAP_RADIO_TX},
    {"ir.send_file", "s", TOS_CAP_RADIO_TX | TOS_CAP_FS_READ},
    {"ir.send_ac", "ybyyy", TOS_CAP_RADIO_TX},
    {"nfc.read_block", "ys", TOS_CAP_RADIO_RX},
    {"nfc.write_block", "yss", TOS_CAP_RADIO_TX},
    {"subghz.get_raw", "u", TOS_CAP_RADIO_RX},
    {"subghz.replay", "s", TOS_CAP_RADIO_TX | TOS_CAP_FS_READ},
    {"lora.mesh_node_count", "", TOS_CAP_RADIO_RX},
    {"lora.mesh_node_get", "u", TOS_CAP_RADIO_RX},
    {"subghz.rx_start", "uy", TOS_CAP_RADIO_RX},
    {"subghz.spectrum_start", "uu", TOS_CAP_RADIO_RX},
    {"subghz.spectrum_poll", "", TOS_CAP_RADIO_RX},
    {"subghz.set_frequency", "u", TOS_CAP_RADIO_RX},
    {"subghz.session_stop", "u", TOS_CAP_RADIO_RX},
    {"nfc.emulate_start", "s", TOS_CAP_RADIO_TX},
    {"nfc.session_stop", "u", TOS_CAP_RADIO_TX},
    {"lora.configure", "uyyyi", 0},
    {"lora.send", "s", TOS_CAP_RADIO_TX},
    {"lora.rx_start", "", TOS_CAP_RADIO_RX},
    {"lora.rx_poll", "", TOS_CAP_RADIO_RX},
    {"lora.mesh_start", "y", TOS_CAP_RADIO_TX},
    {"lora.session_stop", "u", 0},
};
#define SCRIPT_METHOD_COUNT (sizeof(METHODS) / sizeof(METHODS[0]))
_Static_assert(SCRIPT_METHOD_COUNT == SCRIPT_BINDINGS_METHOD_COUNT,
               "ABI dispatch metadata mismatch");

static bool parse_hex_bytes(const char *text, uint8_t *out, size_t size) {
  if (strlen(text) != size * SCRIPT_BYTE_HEX_DIGITS)
    return false;
  for (size_t i = 0; i < size; ++i) {
    uint32_t byte = 0;
    for (size_t j = 0; j < SCRIPT_BYTE_HEX_DIGITS; ++j) {
      char value = text[i * SCRIPT_BYTE_HEX_DIGITS + j];
      uint32_t nibble;
      if (value >= '0' && value <= '9')
        nibble = value - '0';
      else if (value >= 'a' && value <= 'f')
        nibble = value - 'a' + SCRIPT_HEX_LETTER_OFFSET;
      else if (value >= 'A' && value <= 'F')
        nibble = value - 'A' + SCRIPT_HEX_LETTER_OFFSET;
      else
        return false;
      byte = byte * SCRIPT_HEX_RADIX + nibble;
    }
    out[i] = byte;
  }
  return true;
}

static bool parse_mac(const char *text, uint8_t *out_mac) {
  if (strlen(text) != SCRIPT_MAC_TEXT - 1)
    return false;
  for (uint32_t i = 0; i < SCRIPT_MAC_BYTES; ++i) {
    uint32_t value = 0;
    for (uint32_t j = 0; j < SCRIPT_HEX_PREFIX_LENGTH; ++j) {
      char ch = text[i * SCRIPT_MAC_STRIDE + j];
      uint32_t digit = ch >= '0' && ch <= '9'   ? ch - '0'
                       : ch >= 'a' && ch <= 'f' ? ch - 'a' + SCRIPT_HEX_LETTER_OFFSET
                       : ch >= 'A' && ch <= 'F' ? ch - 'A' + SCRIPT_HEX_LETTER_OFFSET
                                                : SCRIPT_HEX_RADIX;
      if (digit >= SCRIPT_HEX_RADIX)
        return false;
      value = value * SCRIPT_HEX_RADIX + digit;
    }
    if (i < SCRIPT_MAC_BYTES - 1 && text[i * SCRIPT_MAC_STRIDE + 2] != ':')
      return false;
    out_mac[i] = (uint8_t)value;
  }
  return true;
}

static bool add_mac(cJSON *object, const char *key, const uint8_t *mac) {
  char text[SCRIPT_MAC_TEXT];
  snprintf(text,
           sizeof(text),
           "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0],
           mac[1],
           mac[2],
           mac[3],
           mac[4],
           mac[5]);
  return cJSON_AddStringToObject(object, key, text) != NULL;
}

esp_err_t script_bindings_requested_caps(const char *source, uint32_t *out_caps) {
  if (source == NULL || out_caps == NULL)
    return ESP_ERR_INVALID_ARG;
  *out_caps = 0;
  if (strncmp(source, "// @caps ", SCRIPT_CAPS_PREFIX_LENGTH) != 0 &&
      strncmp(source, "-- @caps ", SCRIPT_CAPS_PREFIX_LENGTH) != 0)
    return ESP_OK;
  const char *text = source + SCRIPT_CAPS_PREFIX_LENGTH;
  if (strncmp(text, "0x", SCRIPT_HEX_PREFIX_LENGTH) != 0)
    return ESP_ERR_INVALID_ARG;
  text += SCRIPT_HEX_PREFIX_LENGTH;
  uint32_t caps = 0, digits = 0;
  while (*text != '\0' && *text != '\r' && *text != '\n') {
    char ch = *text++;
    uint32_t value = ch >= '0' && ch <= '9'   ? ch - '0'
                     : ch >= 'a' && ch <= 'f' ? ch - 'a' + SCRIPT_HEX_LETTER_OFFSET
                     : ch >= 'A' && ch <= 'F' ? ch - 'A' + SCRIPT_HEX_LETTER_OFFSET
                                              : SCRIPT_HEX_RADIX;
    if (value >= SCRIPT_HEX_RADIX || ++digits > SCRIPT_HEX_DIGITS_MAX)
      return ESP_ERR_INVALID_ARG;
    caps = caps * SCRIPT_HEX_RADIX + value;
  }
  if (digits == 0 || (caps & ~SCRIPT_BINDINGS_CAPS) != 0)
    return ESP_ERR_NOT_SUPPORTED;
  *out_caps = caps;
  return ESP_OK;
}

void script_bindings_close(script_bindings_t *bindings) {
  if (bindings == NULL || bindings->api == NULL)
    return;
  for (uint32_t i = 0; i < SCRIPT_BINDINGS_FILES_MAX; ++i) {
    if (bindings->files[i].file != NULL) {
      bindings->api->fs->close(bindings->files[i].file);
      bindings->files[i].file = NULL;
    }
  }
}

esp_err_t script_bindings_call(script_bindings_t *bindings, script_bindings_call_t *call) {
  if (bindings == NULL || bindings->api == NULL || call == NULL || call->method == NULL)
    return ESP_ERR_INVALID_STATE;
  const tos_api_t *api = bindings->api;
  uint32_t method = 0;
  for (; method < SCRIPT_METHOD_COUNT; ++method)
    if (strcmp(call->method, METHODS[method].name) == 0)
      break;
  if (method == SCRIPT_METHOD_COUNT)
    return ESP_ERR_NOT_SUPPORTED;
  const script_bindings_method_t *entry = &METHODS[method];
  if ((bindings->caps & entry->caps) != entry->caps)
    return ESP_ERR_NOT_SUPPORTED;
  if (call->count > SCRIPT_BINDINGS_ARGS_MAX || call->count != strlen(entry->signature))
    return ESP_ERR_INVALID_ARG;
  uint32_t n[SCRIPT_BINDINGS_ARGS_MAX] = {0};
  const char *s[SCRIPT_BINDINGS_ARGS_MAX] = {0};
  uint8_t mac[SCRIPT_BINDINGS_ARGS_MAX][SCRIPT_MAC_BYTES];
  for (uint32_t i = 0; i < call->count; ++i) {
    script_bindings_value_t *arg = &call->args[i];
    char type = entry->signature[i];
    if (type == 's' || type == 'S' || type == 'm') {
      if (type == 'S' && arg->type == '0')
        continue;
      if (arg->type != 's' || memchr(arg->text, 0, sizeof(arg->text)) == NULL)
        return ESP_ERR_INVALID_ARG;
      s[i] = arg->text;
      if (type == 'm' && !parse_mac(s[i], mac[i]))
        return ESP_ERR_INVALID_ARG;
    } else {
      if (arg->type != (type == 'b' ? 'b' : 'n') || !isfinite(arg->number) ||
          trunc(arg->number) != arg->number)
        return ESP_ERR_INVALID_ARG;
      double minimum = type == 'i' ? INT32_MIN : 0;
      double maximum = type == 'i'   ? INT32_MAX
                       : type == 'y' ? UINT8_MAX
                       : type == 'b' ? 1
                                     : UINT32_MAX;
      if (arg->number < minimum || arg->number > maximum)
        return ESP_ERR_INVALID_ARG;
      n[i] = (uint32_t)(int64_t)arg->number;
    }
  }
  esp_err_t err = ESP_OK;
  double number = 0;
  uint32_t session = 0;
  switch (method) {
    case SCRIPT_BINDINGS_VERSION: {
      call->result = cJSON_CreateObject();
      REQUIRE_JSON(call->result);
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "major", api->abi_major));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "minor", api->abi_minor));
      break;
    }
    case SCRIPT_BINDINGS_SHOULD_STOP: {
      if (api->should_stop == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      call->result = cJSON_CreateBool(api->should_stop());
      REQUIRE_JSON(call->result);
      break;
    }
    case SCRIPT_BINDINGS_RESOURCE_LOST: {
      if (api->resource_lost == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      call->result = cJSON_CreateBool(api->resource_lost());
      REQUIRE_JSON(call->result);
      break;
    }
    case SCRIPT_BINDINGS_YIELD: {
      if (api->yield == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      api->yield();
      break;
    }
    case SCRIPT_BINDINGS_DELAY_MS: {
      if (api->delay_ms == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (n[0] > SCRIPT_DELAY_MAX_MS)
        return ESP_ERR_INVALID_ARG;
      for (uint32_t elapsed = 0; elapsed < n[0] && !api->should_stop(); ++elapsed)
        api->delay_ms(1);
      break;
    }
    case SCRIPT_BINDINGS_LOG: {
      if (api->log == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      api->log(TOS_LOG_INFO, "SCRIPT", "%s", s[0]);
      break;
    }
    case SCRIPT_BINDINGS_LED_SET: {
      if (api->led_set == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->led_set(n[0], n[1], n[2]);
      break;
    }
    case SCRIPT_BINDINGS_WIFI_SCAN: {
      if (api->wifi == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->wifi->scan == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->wifi->scan();
      break;
    }
    case SCRIPT_BINDINGS_WIFI_AP_COUNT: {
      if (api->wifi == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->wifi->ap_count == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      number = api->wifi->ap_count();
      break;
    }
    case SCRIPT_BINDINGS_WIFI_IS_CONNECTED: {
      if (api->wifi == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->wifi->is_connected == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      call->result = cJSON_CreateBool(api->wifi->is_connected());
      REQUIRE_JSON(call->result);
      break;
    }
    case SCRIPT_BINDINGS_WIFI_CONNECTED_SSID: {
      if (api->wifi == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->wifi->connected_ssid == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      const char *text = api->wifi->connected_ssid();
      call->result = cJSON_CreateString(text == NULL ? "" : text);
      REQUIRE_JSON(call->result);
      break;
    }
    case SCRIPT_BINDINGS_WIFI_DISCONNECT: {
      if (api->wifi == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->wifi->disconnect == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->wifi->disconnect();
      break;
    }
    case SCRIPT_BINDINGS_WIFI_DEAUTH_DETECT: {
      if (api->wifi == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->wifi->deauth_detect == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->wifi->deauth_detect(&session);
      number = session;
      break;
    }
    case SCRIPT_BINDINGS_WIFI_PROBE_MONITOR: {
      if (api->wifi == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->wifi->probe_monitor == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->wifi->probe_monitor(&session);
      number = session;
      break;
    }
    case SCRIPT_BINDINGS_WIFI_AP_GET: {
      if (api->wifi == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->wifi->ap_get == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      tos_wifi_ap_t ap = {0};
      err = api->wifi->ap_get((int32_t)n[0], &ap);
      call->result = cJSON_CreateObject();
      REQUIRE_JSON(call->result);
      REQUIRE_JSON(cJSON_AddStringToObject(call->result, "ssid", ap.ssid));
      REQUIRE_JSON(add_mac(call->result, "bssid", ap.bssid));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "rssi", ap.rssi));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "channel", ap.channel));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "authmode", ap.authmode));
      break;
    }
    case SCRIPT_BINDINGS_WIFI_CONNECT: {
      if (api->wifi == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->wifi->connect == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->wifi->connect(s[0], s[1]);
      break;
    }
    case SCRIPT_BINDINGS_WIFI_SNIFFER_START: {
      if (api->wifi == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->wifi->sniffer_start == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->wifi->sniffer_start(n[0], n[1], n[2], &session);
      number = session;
      break;
    }
    case SCRIPT_BINDINGS_WIFI_SIGNAL_MONITOR: {
      if (api->wifi == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->wifi->signal_monitor == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->wifi->signal_monitor(mac[0], n[1], &session);
      number = session;
      break;
    }
    case SCRIPT_BINDINGS_WIFI_DEAUTH: {
      if (api->wifi == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->wifi->deauth == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->wifi->deauth(mac[0], mac[1], n[2], n[3], &session);
      number = session;
      break;
    }
    case SCRIPT_BINDINGS_WIFI_DEAUTH_BROADCAST: {
      if (api->wifi == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->wifi->deauth_broadcast == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->wifi->deauth_broadcast(mac[0], n[1], n[2]);
      break;
    }
    case SCRIPT_BINDINGS_WIFI_FLOOD: {
      if (api->wifi == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->wifi->flood == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->wifi->flood(n[0], mac[1], n[2], &session);
      number = session;
      break;
    }
    case SCRIPT_BINDINGS_WIFI_BEACON_SPAM: {
      if (api->wifi == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->wifi->beacon_spam == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->wifi->beacon_spam(s[0], &session);
      number = session;
      break;
    }
    case SCRIPT_BINDINGS_WIFI_EVIL_TWIN: {
      if (api->wifi == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->wifi->evil_twin == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->wifi->evil_twin(s[0], &session);
      number = session;
      break;
    }
    case SCRIPT_BINDINGS_WIFI_SESSION_STOP: {
      if (api->wifi == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->wifi->session_stop == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->wifi->session_stop(n[0]);
      break;
    }
    case SCRIPT_BINDINGS_WIFI_CHANNEL_HOP: {
      if (api->wifi == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->wifi->channel_hop == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->wifi->channel_hop(n[0]);
      break;
    }
    case SCRIPT_BINDINGS_BLE_DEVICE_COUNT: {
      if (api->ble == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ble->device_count == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      number = api->ble->device_count();
      break;
    }
    case SCRIPT_BINDINGS_BLE_DISCONNECT: {
      if (api->ble == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ble->disconnect == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ble->disconnect();
      break;
    }
    case SCRIPT_BINDINGS_BLE_SET_RANDOM_MAC: {
      if (api->ble == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ble->set_random_mac == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ble->set_random_mac();
      break;
    }
    case SCRIPT_BINDINGS_BLE_ADV_START: {
      if (api->ble == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ble->adv_start == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ble->adv_start();
      break;
    }
    case SCRIPT_BINDINGS_BLE_ADV_STOP: {
      if (api->ble == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ble->adv_stop == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ble->adv_stop();
      break;
    }
    case SCRIPT_BINDINGS_BLE_SNIFFER_START: {
      if (api->ble == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ble->sniffer_start == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ble->sniffer_start(&session);
      number = session;
      break;
    }
    case SCRIPT_BINDINGS_BLE_SKIMMER_DETECT: {
      if (api->ble == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ble->skimmer_detect == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ble->skimmer_detect(&session);
      number = session;
      break;
    }
    case SCRIPT_BINDINGS_BLE_TRACKER_DETECT: {
      if (api->ble == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ble->tracker_detect == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ble->tracker_detect(&session);
      number = session;
      break;
    }
    case SCRIPT_BINDINGS_BLE_HID_START: {
      if (api->ble == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ble->hid_start == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ble->hid_start();
      break;
    }
    case SCRIPT_BINDINGS_BLE_HID_STOP: {
      if (api->ble == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ble->hid_stop == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ble->hid_stop();
      break;
    }
    case SCRIPT_BINDINGS_BLE_HID_IS_CONNECTED: {
      if (api->ble == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ble->hid_is_connected == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      call->result = cJSON_CreateBool(api->ble->hid_is_connected());
      REQUIRE_JSON(call->result);
      break;
    }
    case SCRIPT_BINDINGS_BLE_SCAN: {
      if (api->ble == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ble->scan == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (n[0] > SCRIPT_DELAY_MAX_MS)
        return ESP_ERR_INVALID_ARG;
      err = api->ble->scan(n[0]);
      break;
    }
    case SCRIPT_BINDINGS_BLE_DEVICE_GET: {
      if (api->ble == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ble->device_get == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      tos_ble_device_t device = {0};
      err = api->ble->device_get((int32_t)n[0], &device);
      call->result = cJSON_CreateObject();
      REQUIRE_JSON(call->result);
      REQUIRE_JSON(cJSON_AddStringToObject(call->result, "name", device.name));
      REQUIRE_JSON(add_mac(call->result, "addr", device.addr));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "addr_type", device.addr_type));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "rssi", device.rssi));
      break;
    }
    case SCRIPT_BINDINGS_BLE_CONNECT: {
      if (api->ble == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ble->connect == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ble->connect(mac[0], n[1]);
      break;
    }
    case SCRIPT_BINDINGS_BLE_SPAM: {
      if (api->ble == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ble->spam == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ble->spam(n[0], &session);
      number = session;
      break;
    }
    case SCRIPT_BINDINGS_BLE_FLOOD: {
      if (api->ble == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ble->flood == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ble->flood(mac[0], n[1], &session);
      number = session;
      break;
    }
    case SCRIPT_BINDINGS_BLE_SESSION_STOP: {
      if (api->ble == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ble->session_stop == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ble->session_stop(n[0]);
      break;
    }
    case SCRIPT_BINDINGS_BLE_HID_SEND_KEY: {
      if (api->ble == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ble->hid_send_key == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ble->hid_send_key(n[0], n[1]);
      break;
    }
    case SCRIPT_BINDINGS_UI_SCREEN_OPEN: {
      if (api->ui == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ui->screen_open == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (bindings->display != NULL)
        bindings->display(true);
      err = api->ui->screen_open();
      if (err != ESP_OK && bindings->display != NULL)
        bindings->display(false);
      break;
    }
    case SCRIPT_BINDINGS_UI_SCREEN_CLOSE: {
      if (api->ui == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ui->screen_close == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      api->ui->screen_close();
      if (bindings->display != NULL)
        bindings->display(false);
      break;
    }
    case SCRIPT_BINDINGS_UI_METRICS: {
      if (api->ui == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ui->metrics == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      int32_t width, height;
      api->ui->metrics(&width, &height);
      call->result = cJSON_CreateObject();
      REQUIRE_JSON(call->result);
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "width", width));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "height", height));
      break;
    }
    case SCRIPT_BINDINGS_UI_LABEL: {
      if (api->ui == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ui->label == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      number = api->ui->label(n[0], (int32_t)n[1], (int32_t)n[2], s[3]);
      if (number == TOS_UI_NONE)
        err = ESP_ERR_INVALID_STATE;
      break;
    }
    case SCRIPT_BINDINGS_UI_RECT: {
      if (api->ui == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ui->rect == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      number = api->ui->rect(n[0], (int32_t)n[1], (int32_t)n[2], (int32_t)n[3], (int32_t)n[4]);
      if (number == TOS_UI_NONE)
        err = ESP_ERR_INVALID_STATE;
      break;
    }
    case SCRIPT_BINDINGS_UI_BAR: {
      if (api->ui == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ui->bar == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      number = api->ui->bar(n[0],
                            (int32_t)n[1],
                            (int32_t)n[2],
                            (int32_t)n[3],
                            (int32_t)n[4],
                            (int32_t)n[5],
                            (int32_t)n[6]);
      if (number == TOS_UI_NONE)
        err = ESP_ERR_INVALID_STATE;
      break;
    }
    case SCRIPT_BINDINGS_UI_SET_TEXT: {
      if (api->ui == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ui->set_text == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ui->set_text(n[0], s[1]);
      break;
    }
    case SCRIPT_BINDINGS_UI_SET_VALUE: {
      if (api->ui == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ui->set_value == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ui->set_value(n[0], (int32_t)n[1]);
      break;
    }
    case SCRIPT_BINDINGS_UI_SET_POS: {
      if (api->ui == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ui->set_pos == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ui->set_pos(n[0], (int32_t)n[1], (int32_t)n[2]);
      break;
    }
    case SCRIPT_BINDINGS_UI_SET_SIZE: {
      if (api->ui == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ui->set_size == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ui->set_size(n[0], (int32_t)n[1], (int32_t)n[2]);
      break;
    }
    case SCRIPT_BINDINGS_UI_SET_TEXT_COLOR: {
      if (api->ui == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ui->set_text_color == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ui->set_text_color(n[0], n[1]);
      break;
    }
    case SCRIPT_BINDINGS_UI_SET_BG_COLOR: {
      if (api->ui == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ui->set_bg_color == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ui->set_bg_color(n[0], n[1]);
      break;
    }
    case SCRIPT_BINDINGS_UI_SET_HIDDEN: {
      if (api->ui == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ui->set_hidden == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ui->set_hidden(n[0], n[1]);
      break;
    }
    case SCRIPT_BINDINGS_UI_DEL: {
      if (api->ui == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ui->del == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ui->del(n[0]);
      break;
    }
    case SCRIPT_BINDINGS_UI_POLL_EVENT: {
      if (api->ui == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ui->poll_event == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      tos_ui_event_t event = {0};
      number = api->ui->poll_event(&event);
      if (number) {
        call->result = cJSON_CreateObject();
        REQUIRE_JSON(call->result);
        REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "key", event.key));
        REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "action", event.action));
      } else {
        call->result = cJSON_CreateNull();
        REQUIRE_JSON(call->result);
      }
      break;
    }
    case SCRIPT_BINDINGS_UI_CANVAS_END: {
      if (api->ui == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->ui->canvas_end == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      api->ui->canvas_end();
      if (bindings->display != NULL)
        bindings->display(false);
      break;
    }
    case SCRIPT_BINDINGS_FS_SIZE: {
      if (api->fs == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->fs->size == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      number = api->fs->size(s[0]);
      if (number < 0)
        err = ESP_FAIL;
      break;
    }
    case SCRIPT_BINDINGS_FS_LIST: {
      if (api->fs == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->fs->list == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (n[1] > SCRIPT_BINDINGS_LIST_MAX || n[1] == 0)
        return ESP_ERR_INVALID_ARG;
      tos_fs_dirent_t entries[SCRIPT_BINDINGS_LIST_MAX];
      int count = api->fs->list(s[0], entries, n[1]);
      if (count < 0)
        return ESP_FAIL;
      if (count > n[1])
        return ESP_ERR_INVALID_SIZE;
      call->result = cJSON_CreateArray();
      REQUIRE_JSON(call->result);
      for (int i = 0; i < count; ++i) {
        cJSON *entry = cJSON_CreateObject();
        REQUIRE_JSON(entry);
        if (!cJSON_AddItemToArray(call->result, entry)) {
          cJSON_Delete(entry);
          return ESP_ERR_NO_MEM;
        }
        REQUIRE_JSON(cJSON_AddStringToObject(entry, "name", entries[i].name));
        REQUIRE_JSON(cJSON_AddNumberToObject(entry, "size", entries[i].size));
        REQUIRE_JSON(cJSON_AddBoolToObject(entry, "is_dir", entries[i].is_dir));
      }
      break;
    }
    case SCRIPT_BINDINGS_FS_OPEN: {
      if (api->fs == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->fs->open == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      uint32_t slot = 0;
      while (slot < SCRIPT_BINDINGS_FILES_MAX && bindings->files[slot].file != NULL)
        ++slot;
      if (slot == SCRIPT_BINDINGS_FILES_MAX || bindings->next_token == UINT32_MAX)
        return ESP_ERR_NO_MEM;
      void *file = api->fs->open(s[0]);
      if (file == NULL)
        return ESP_FAIL;
      bindings->files[slot].file = file;
      bindings->files[slot].token = ++bindings->next_token;
      number = bindings->next_token;
      break;
    }
    case SCRIPT_BINDINGS_FS_READ: {
      if (api->fs == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->fs->read == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      uint32_t slot = 0;
      while (slot < SCRIPT_BINDINGS_FILES_MAX &&
             (bindings->files[slot].file == NULL || bindings->files[slot].token != n[0]))
        ++slot;
      if (slot == SCRIPT_BINDINGS_FILES_MAX)
        return ESP_ERR_INVALID_ARG;
      void *file = bindings->files[slot].file;
      if (n[1] > SCRIPT_BINDINGS_READ_MAX)
        return ESP_ERR_INVALID_ARG;
      uint8_t data[SCRIPT_BINDINGS_READ_MAX];
      int count = api->fs->read(file, data, n[1]);
      if (count < 0)
        return ESP_FAIL;
      if (count > n[1])
        return ESP_ERR_INVALID_SIZE;
      call->result = cJSON_CreateArray();
      REQUIRE_JSON(call->result);
      for (int i = 0; i < count; ++i) {
        cJSON *byte = cJSON_CreateNumber(data[i]);
        REQUIRE_JSON(byte);
        if (!cJSON_AddItemToArray(call->result, byte)) {
          cJSON_Delete(byte);
          return ESP_ERR_NO_MEM;
        }
      }
      break;
    }
    case SCRIPT_BINDINGS_FS_SEEK: {
      if (api->fs == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->fs->seek == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      uint32_t slot = 0;
      while (slot < SCRIPT_BINDINGS_FILES_MAX &&
             (bindings->files[slot].file == NULL || bindings->files[slot].token != n[0]))
        ++slot;
      if (slot == SCRIPT_BINDINGS_FILES_MAX)
        return ESP_ERR_INVALID_ARG;
      void *file = bindings->files[slot].file;
      if (n[1] > INT32_MAX)
        return ESP_ERR_INVALID_ARG;
      if (api->fs->seek(file, n[1]) < 0)
        err = ESP_FAIL;
      break;
    }
    case SCRIPT_BINDINGS_FS_CLOSE: {
      if (api->fs == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (api->fs->close == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      uint32_t slot = 0;
      while (slot < SCRIPT_BINDINGS_FILES_MAX &&
             (bindings->files[slot].file == NULL || bindings->files[slot].token != n[0]))
        ++slot;
      if (slot == SCRIPT_BINDINGS_FILES_MAX)
        return ESP_ERR_INVALID_ARG;
      void *file = bindings->files[slot].file;
      api->fs->close(file);
      bindings->files[slot].file = NULL;
      break;
    }
    case SCRIPT_BINDINGS_NFC_POLL: {
      if (api->abi_minor < SCRIPT_RF_ABI_MINOR || api->nfc == NULL || api->nfc->poll == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      tos_nfc_tag_t tag = {0};
      err = api->nfc->poll(&tag, n[0]);
      if (err != ESP_OK)
        break;
      if (tag.uid_len > sizeof(tag.uid))
        return ESP_ERR_INVALID_SIZE;
      call->result = cJSON_CreateObject();
      REQUIRE_JSON(call->result);
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "protocol", tag.protocol));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "uid_len", tag.uid_len));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "sak", tag.sak));
      cJSON *uid = cJSON_AddArrayToObject(call->result, "uid");
      REQUIRE_JSON(uid);
      for (uint32_t index = 0; index < tag.uid_len; ++index) {
        cJSON *byte = cJSON_CreateNumber(tag.uid[index]);
        REQUIRE_JSON(byte);
        if (!cJSON_AddItemToArray(uid, byte)) {
          cJSON_Delete(byte);
          goto no_memory;
        }
      }
      cJSON *atqa = cJSON_AddArrayToObject(call->result, "atqa");
      REQUIRE_JSON(atqa);
      for (uint32_t index = 0; index < sizeof(tag.atqa); ++index) {
        cJSON *byte = cJSON_CreateNumber(tag.atqa[index]);
        REQUIRE_JSON(byte);
        if (!cJSON_AddItemToArray(atqa, byte)) {
          cJSON_Delete(byte);
          goto no_memory;
        }
      }
      break;
    }
    case SCRIPT_BINDINGS_NFC_EMULATE_START: {
      if (api->abi_minor < SCRIPT_NFC_SESSION_ABI_MINOR || api->nfc == NULL ||
          api->nfc->emulate_start == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      uint8_t uid[SCRIPT_NFC_UID_BYTES];
      uint32_t session = 0;
      if (!parse_hex_bytes(s[0], uid, sizeof(uid)))
        return ESP_ERR_INVALID_ARG;
      err = api->nfc->emulate_start(uid, sizeof(uid), &session);
      if (err != ESP_OK)
        break;
      call->result = cJSON_CreateNumber(session);
      REQUIRE_JSON(call->result);
      break;
    }
    case SCRIPT_BINDINGS_NFC_SESSION_STOP:
      if (api->abi_minor < SCRIPT_NFC_SESSION_ABI_MINOR || api->nfc == NULL ||
          api->nfc->session_stop == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->nfc->session_stop(n[0]);
      break;
    case SCRIPT_BINDINGS_NFC_READ_BLOCK:
    case SCRIPT_BINDINGS_NFC_WRITE_BLOCK: {
      if (api->abi_minor < SCRIPT_NFC_BLOCK_ABI_MINOR || api->nfc == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      bool is_write = method == SCRIPT_BINDINGS_NFC_WRITE_BLOCK;
      if ((is_write && api->nfc->write_block == NULL) ||
          (!is_write && api->nfc->read_block == NULL))
        return ESP_ERR_NOT_SUPPORTED;
      uint8_t key[SCRIPT_NFC_KEY_BYTES], data[SCRIPT_NFC_BLOCK_BYTES];
      if (!parse_hex_bytes(s[1], key, sizeof(key)) ||
          (is_write && !parse_hex_bytes(s[2], data, sizeof(data))))
        return ESP_ERR_INVALID_ARG;
      err =
          is_write ? api->nfc->write_block(n[0], key, data) : api->nfc->read_block(n[0], key, data);
      if (err != ESP_OK || is_write)
        break;
      call->result = cJSON_CreateArray();
      REQUIRE_JSON(call->result);
      for (size_t i = 0; i < sizeof(data); ++i) {
        cJSON *byte = cJSON_CreateNumber(data[i]);
        REQUIRE_JSON(byte);
        cJSON_AddItemToArray(call->result, byte);
      }
      break;
    }
    case SCRIPT_BINDINGS_IR_RECEIVE: {
      if (api->abi_minor < SCRIPT_IR_ABI_MINOR || api->ir == NULL || api->ir->receive == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      tos_ir_code_t code = {0};
      err = api->ir->receive(&code, n[0]);
      if (err != ESP_OK)
        break;
      call->result = cJSON_CreateObject();
      REQUIRE_JSON(call->result);
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "protocol", code.protocol));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "address", code.address));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "command", code.command));
      REQUIRE_JSON(cJSON_AddBoolToObject(call->result, "repeat", code.repeat));
      break;
    }
    case SCRIPT_BINDINGS_IR_SEND: {
      if (api->abi_minor < SCRIPT_IR_ABI_MINOR || api->ir == NULL || api->ir->send == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      tos_ir_code_t code = {
          .protocol = n[0], .address = n[1], .command = n[2], .repeat = n[3] != 0};
      err = api->ir->send(&code);
      break;
    }
    case SCRIPT_BINDINGS_SUBGHZ_POLL:
    case SCRIPT_BINDINGS_SUBGHZ_RECEIVE: {
      if (api->abi_minor < SCRIPT_RADIO_VIEW_ABI_MINOR || api->subghz == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      tos_subghz_signal_t signal = {0};
      if (method == SCRIPT_BINDINGS_SUBGHZ_RECEIVE) {
        if (api->abi_minor < SCRIPT_SUBGHZ_ABI_MINOR || api->subghz->receive == NULL)
          return ESP_ERR_NOT_SUPPORTED;
        err = api->subghz->receive(&signal, n[0], n[1], n[2]);
        if (err != ESP_OK)
          break;
      } else {
        if (api->subghz->rx_poll == NULL)
          return ESP_ERR_NOT_SUPPORTED;
        int count = api->subghz->rx_poll(&signal);
        if (count < 0)
          return -count;
        if (count == 0) {
          call->result = cJSON_CreateNull();
          REQUIRE_JSON(call->result);
          break;
        }
      }
      signal.protocol[sizeof(signal.protocol) - 1] = '\0';
      call->result = cJSON_CreateObject();
      REQUIRE_JSON(call->result);
      REQUIRE_JSON(cJSON_AddStringToObject(call->result, "protocol", signal.protocol));
      REQUIRE_JSON(cJSON_AddBoolToObject(call->result, "decoded", signal.decoded));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "freq_hz", signal.freq_hz));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "value", signal.value));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "serial", signal.serial));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "bit_count", signal.bit_count));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "btn", signal.btn));
      break;
    }
    case SCRIPT_BINDINGS_IR_SEND_RAW:
    case SCRIPT_BINDINGS_SUBGHZ_TX_RAW: {
      bool is_ir = method == SCRIPT_BINDINGS_IR_SEND_RAW;
      if (is_ir) {
        if (api->abi_minor < SCRIPT_IR_EXT_ABI_MINOR || api->ir == NULL ||
            api->ir->send_raw == NULL)
          return ESP_ERR_NOT_SUPPORTED;
      } else if (api->abi_minor < SCRIPT_SUBGHZ_ABI_MINOR || api->subghz == NULL ||
                 api->subghz->tx_raw == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      int32_t pulses[SCRIPT_SUBGHZ_PULSES_MAX];
      uint32_t count = 0;
      const char *text = s[1];
      while (*text != '\0') {
        if (count == SCRIPT_SUBGHZ_PULSES_MAX ||
            (*text != '-' && *text != '+' && (*text < '0' || *text > '9')))
          return ESP_ERR_INVALID_ARG;
        char *end;
        long pulse = strtol(text, &end, SCRIPT_DECIMAL_BASE);
        if (end == text || pulse == 0 || pulse < -SCRIPT_SUBGHZ_PULSE_MAX_US ||
            pulse > SCRIPT_SUBGHZ_PULSE_MAX_US || (*end != '\0' && *end != ','))
          return ESP_ERR_INVALID_ARG;
        pulses[count++] = (int32_t)pulse;
        text = *end == ',' ? end + 1 : end;
        if (*end == ',' && *text == '\0')
          return ESP_ERR_INVALID_ARG;
      }
      if (count == 0)
        return ESP_ERR_INVALID_ARG;
      err =
          is_ir ? api->ir->send_raw(pulses, count, n[0]) : api->subghz->tx_raw(pulses, count, n[0]);
      break;
    }
    case SCRIPT_BINDINGS_SUBGHZ_GET_RAW:
    case SCRIPT_BINDINGS_IR_GET_RAW: {
      bool is_subghz = method == SCRIPT_BINDINGS_SUBGHZ_GET_RAW;
      if (is_subghz) {
        if (api->abi_minor < SCRIPT_SUBGHZ_RAW_ABI_MINOR || api->subghz == NULL ||
            api->subghz->get_raw == NULL)
          return ESP_ERR_NOT_SUPPORTED;
      } else if (api->abi_minor < SCRIPT_IR_EXT_ABI_MINOR || api->ir == NULL ||
                 api->ir->get_last_raw == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (n[0] == 0 || n[0] > SCRIPT_SUBGHZ_PULSES_MAX)
        return ESP_ERR_INVALID_ARG;
      int32_t pulses[SCRIPT_SUBGHZ_PULSES_MAX];
      int count =
          is_subghz ? api->subghz->get_raw(pulses, n[0]) : api->ir->get_last_raw(pulses, n[0]);
      if (count < 0)
        return -count;
      if ((uint32_t)count > n[0])
        return ESP_ERR_INVALID_SIZE;
      call->result = cJSON_CreateArray();
      REQUIRE_JSON(call->result);
      for (int i = 0; i < count; ++i) {
        cJSON *pulse = cJSON_CreateNumber(pulses[i]);
        REQUIRE_JSON(pulse);
        cJSON_AddItemToArray(call->result, pulse);
      }
      break;
    }
    case SCRIPT_BINDINGS_SUBGHZ_REPLAY:
      if (api->abi_minor < SCRIPT_SUBGHZ_REPLAY_ABI_MINOR || api->subghz == NULL ||
          api->subghz->replay == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->subghz->replay(s[0]);
      break;
    case SCRIPT_BINDINGS_IR_SEND_FILE:
      if (api->abi_minor < SCRIPT_IR_EXT_ABI_MINOR || api->ir == NULL || api->ir->send_file == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->ir->send_file(s[0]);
      break;
    case SCRIPT_BINDINGS_IR_SEND_AC: {
      if (api->abi_minor < SCRIPT_IR_EXT_ABI_MINOR || api->ir == NULL || api->ir->send_ac == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      tos_ir_ac_t state = {
          .protocol = n[0], .power = n[1] != 0, .mode = n[2], .temp_c = n[3], .fan = n[4]};
      err = api->ir->send_ac(&state);
      break;
    }
    case SCRIPT_BINDINGS_SUBGHZ_RX_START:
    case SCRIPT_BINDINGS_SUBGHZ_SPECTRUM_START: {
      if (api->abi_minor < SCRIPT_SUBGHZ_SESSION_ABI_MINOR || api->subghz == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      uint32_t session = 0;
      if (method == SCRIPT_BINDINGS_SUBGHZ_RX_START) {
        if (api->subghz->rx_start == NULL)
          return ESP_ERR_NOT_SUPPORTED;
        err = api->subghz->rx_start(n[0], n[1], &session);
      } else {
        if (api->subghz->spectrum_start == NULL)
          return ESP_ERR_NOT_SUPPORTED;
        err = api->subghz->spectrum_start(n[0], n[1], &session);
      }
      if (err != ESP_OK)
        break;
      call->result = cJSON_CreateNumber(session);
      REQUIRE_JSON(call->result);
      break;
    }
    case SCRIPT_BINDINGS_SUBGHZ_SET_FREQUENCY:
    case SCRIPT_BINDINGS_SUBGHZ_SESSION_STOP:
      if (api->abi_minor < SCRIPT_SUBGHZ_SESSION_ABI_MINOR || api->subghz == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (method == SCRIPT_BINDINGS_SUBGHZ_SET_FREQUENCY) {
        if (api->subghz->set_frequency == NULL)
          return ESP_ERR_NOT_SUPPORTED;
        err = api->subghz->set_frequency(n[0]);
      } else {
        if (api->subghz->session_stop == NULL)
          return ESP_ERR_NOT_SUPPORTED;
        err = api->subghz->session_stop(n[0]);
      }
      break;
    case SCRIPT_BINDINGS_SUBGHZ_SPECTRUM_POLL:
    case SCRIPT_BINDINGS_SUBGHZ_SPECTRUM_CAPTURE: {
      tos_subghz_spectrum_t spectrum = {0};
      if (method == SCRIPT_BINDINGS_SUBGHZ_SPECTRUM_POLL) {
        if (api->abi_minor < SCRIPT_SUBGHZ_SESSION_ABI_MINOR || api->subghz == NULL ||
            api->subghz->spectrum_poll == NULL)
          return ESP_ERR_NOT_SUPPORTED;
        int count = api->subghz->spectrum_poll(
            &spectrum.start_hz, &spectrum.step_hz, spectrum.dbm, TOS_SUBGHZ_SPECTRUM_BINS);
        if (count < 0)
          return -count;
        if (count == 0) {
          call->result = cJSON_CreateNull();
          REQUIRE_JSON(call->result);
          break;
        }
        if (count != TOS_SUBGHZ_SPECTRUM_BINS)
          return ESP_ERR_INVALID_SIZE;
      } else {
        if (api->abi_minor < SCRIPT_SPECTRUM_ABI_MINOR || api->subghz == NULL ||
            api->subghz->spectrum_capture == NULL)
          return ESP_ERR_NOT_SUPPORTED;
        err = api->subghz->spectrum_capture(&spectrum, n[0], n[1]);
        if (err != ESP_OK)
          break;
      }
      call->result = cJSON_CreateObject();
      REQUIRE_JSON(call->result);
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "start_hz", spectrum.start_hz));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "step_hz", spectrum.step_hz));
      cJSON *bins = cJSON_AddArrayToObject(call->result, "dbm");
      REQUIRE_JSON(bins);
      for (uint32_t i = 0; i < TOS_SUBGHZ_SPECTRUM_BINS; ++i) {
        cJSON *bin = cJSON_CreateNumber(spectrum.dbm[i]);
        REQUIRE_JSON(bin);
        if (!cJSON_AddItemToArray(bins, bin)) {
          cJSON_Delete(bin);
          goto no_memory;
        }
      }
      break;
    }

    case SCRIPT_BINDINGS_LORA_CONFIGURE: {
      if (api->abi_minor < SCRIPT_LORA_SESSION_ABI_MINOR || api->lora == NULL ||
          api->lora->configure == NULL ||
          (bindings->caps & (TOS_CAP_RADIO_RX | TOS_CAP_RADIO_TX)) == 0)
        return ESP_ERR_NOT_SUPPORTED;
      int32_t power = (int32_t)n[4];
      if (power < SCRIPT_LORA_POWER_MIN_DBM || power > SCRIPT_LORA_POWER_MAX_DBM)
        return ESP_ERR_INVALID_ARG;
      tos_lora_cfg_t config = {
          .freq_hz = n[0], .sf = n[1], .bw = n[2], .cr = n[3], .tx_power_dbm = power};
      err = api->lora->configure(&config);
      break;
    }
    case SCRIPT_BINDINGS_LORA_SEND: {
      if (api->abi_minor < SCRIPT_LORA_SESSION_ABI_MINOR || api->lora == NULL ||
          api->lora->send == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      size_t length = strlen(s[0]);
      if (length == 0 || length > SCRIPT_LORA_DATA_MAX * SCRIPT_BYTE_HEX_DIGITS ||
          length % SCRIPT_BYTE_HEX_DIGITS != 0)
        return ESP_ERR_INVALID_ARG;
      uint8_t data[SCRIPT_LORA_DATA_MAX];
      length /= SCRIPT_BYTE_HEX_DIGITS;
      if (!parse_hex_bytes(s[0], data, length))
        return ESP_ERR_INVALID_ARG;
      err = api->lora->send(data, length);
      break;
    }
    case SCRIPT_BINDINGS_LORA_RX_START:
    case SCRIPT_BINDINGS_LORA_MESH_START:
      if (api->abi_minor < SCRIPT_LORA_SESSION_ABI_MINOR || api->lora == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (method == SCRIPT_BINDINGS_LORA_RX_START) {
        if (api->lora->rx_start == NULL)
          return ESP_ERR_NOT_SUPPORTED;
        err = api->lora->rx_start(&session);
      } else {
        if (api->lora->mesh_start == NULL)
          return ESP_ERR_NOT_SUPPORTED;
        err = api->lora->mesh_start(n[0], &session);
      }
      if (err != ESP_OK)
        break;
      call->result = cJSON_CreateNumber(session);
      REQUIRE_JSON(call->result);
      break;
    case SCRIPT_BINDINGS_LORA_SESSION_STOP:
      if (api->abi_minor < SCRIPT_LORA_SESSION_ABI_MINOR || api->lora == NULL ||
          api->lora->session_stop == NULL ||
          (bindings->caps & (TOS_CAP_RADIO_RX | TOS_CAP_RADIO_TX)) == 0)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->lora->session_stop(n[0]);
      break;
    case SCRIPT_BINDINGS_LORA_RX_POLL: {
      if (api->abi_minor < SCRIPT_LORA_SESSION_ABI_MINOR || api->lora == NULL ||
          api->lora->rx_poll == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      tos_lora_packet_t packet;
      int count = api->lora->rx_poll(&packet);
      if (count < 0)
        return count == ESP_FAIL ? ESP_FAIL : -count;
      if (count == 0) {
        call->result = cJSON_CreateNull();
        REQUIRE_JSON(call->result);
        break;
      }
      call->result = cJSON_CreateObject();
      REQUIRE_JSON(call->result);
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "rssi_dbm", packet.rssi_dbm));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "snr_db", packet.snr_db));
      cJSON *data = cJSON_AddArrayToObject(call->result, "data");
      REQUIRE_JSON(data);
      for (size_t i = 0; i < packet.len; ++i) {
        cJSON *byte = cJSON_CreateNumber(packet.data[i]);
        REQUIRE_JSON(byte);
        if (!cJSON_AddItemToArray(data, byte)) {
          cJSON_Delete(byte);
          goto no_memory;
        }
      }
      break;
    }
    case SCRIPT_BINDINGS_LORA_MESH_SEND_TEXT: {
      if (api->abi_minor < SCRIPT_LORA_TX_ABI_MINOR || api->lora == NULL ||
          api->lora->mesh_send_text == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      err = api->lora->mesh_send_text(s[0]);
      break;
    }
    case SCRIPT_BINDINGS_LORA_NODE_COUNT: {
      if (api->abi_minor < SCRIPT_LORA_NODES_ABI_MINOR || api->lora == NULL ||
          api->lora->mesh_node_count == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      int count = api->lora->mesh_node_count();
      if (count < 0)
        return -count;
      call->result = cJSON_CreateNumber(count);
      REQUIRE_JSON(call->result);
      break;
    }
    case SCRIPT_BINDINGS_LORA_NODE_GET: {
      if (api->abi_minor < SCRIPT_LORA_NODES_ABI_MINOR || api->lora == NULL ||
          api->lora->mesh_node_get == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      if (n[0] > UINT16_MAX)
        return ESP_ERR_INVALID_ARG;
      tos_lora_node_t node = {0};
      err = api->lora->mesh_node_get((int)n[0], &node);
      if (err != ESP_OK)
        break;
      node.name[sizeof(node.name) - 1] = '\0';
      call->result = cJSON_CreateObject();
      REQUIRE_JSON(call->result);
      REQUIRE_JSON(cJSON_AddStringToObject(call->result, "name", node.name));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "rssi_dbm", node.rssi_dbm));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "snr_db", node.snr_db));
      break;
    }
    case SCRIPT_BINDINGS_LORA_MESH_POLL: {
      if (api->abi_minor < SCRIPT_RADIO_VIEW_ABI_MINOR || api->lora == NULL ||
          api->lora->mesh_poll == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      tos_lora_msg_t message = {0};
      int count = api->lora->mesh_poll(&message);
      if (count < 0)
        return -count;
      if (count == 0) {
        call->result = cJSON_CreateNull();
        REQUIRE_JSON(call->result);
        break;
      }
      message.who[sizeof(message.who) - 1] = '\0';
      message.text[sizeof(message.text) - 1] = '\0';
      call->result = cJSON_CreateObject();
      REQUIRE_JSON(call->result);
      REQUIRE_JSON(cJSON_AddBoolToObject(call->result, "outgoing", message.outgoing));
      REQUIRE_JSON(cJSON_AddStringToObject(call->result, "who", message.who));
      REQUIRE_JSON(cJSON_AddStringToObject(call->result, "text", message.text));
      break;
    }
    case SCRIPT_BINDINGS_RFID_READ: {
      if (api->abi_minor < SCRIPT_RF_ABI_MINOR || api->rfid == NULL || api->rfid->read == NULL)
        return ESP_ERR_NOT_SUPPORTED;
      tos_rfid_tag_t tag = {0};
      err = api->rfid->read(&tag, n[0]);
      if (err != ESP_OK)
        break;
      tag.id[sizeof(tag.id) - 1] = 0;
      tag.protocol[sizeof(tag.protocol) - 1] = 0;
      call->result = cJSON_CreateObject();
      REQUIRE_JSON(call->result);
      REQUIRE_JSON(cJSON_AddStringToObject(call->result, "id", tag.id));
      REQUIRE_JSON(cJSON_AddStringToObject(call->result, "protocol", tag.protocol));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "bit_count", tag.bit_count));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "raw_value", tag.raw_value));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "card_number", tag.card_number));
      REQUIRE_JSON(cJSON_AddNumberToObject(call->result, "facility_code", tag.facility_code));
      break;
    }
    default:
      return ESP_ERR_NOT_SUPPORTED;
  }
  if (err != ESP_OK)
    return err;
  if (call->result == NULL)
    call->result = cJSON_CreateNumber(number);
  REQUIRE_JSON(call->result);
  return ESP_OK;
no_memory:
  ESP_LOGE(TAG, "Cannot allocate ABI result");
  return ESP_ERR_NO_MEM;
}
