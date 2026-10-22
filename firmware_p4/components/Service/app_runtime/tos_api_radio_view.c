// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "tos_api.h"

#include <string.h>

#include "esp_log.h"

#include "host_link_local_rf.h"
#include "lora_session.h"
#include "subghz_receiver.h"
#include "tos_app_ctx.h"
#include "tos_api_subghz.h"

#define RADIO_VIEW_LOCK_MS 100

static const char *TAG = "TOS_RADIO_VIEW";
static uint32_t s_subghz_sequence;
static uint32_t s_lora_sequence;

static esp_err_t acquire_view(void) {
  if (!tos_app_cap_check(TOS_CAP_RADIO_RX))
    return ESP_ERR_NOT_SUPPORTED;
  if (tos_api_get()->should_stop() || !host_local_rf_is_ready())
    return ESP_ERR_INVALID_STATE;
  if (!host_local_rf_lock(RADIO_VIEW_LOCK_MS))
    return ESP_ERR_TIMEOUT;
  if (tos_api_get()->should_stop()) {
    host_local_rf_unlock();
    return ESP_ERR_INVALID_STATE;
  }
  return ESP_OK;
}

static int poll_subghz(tos_subghz_signal_t *out) {
  if (out == NULL)
    return -ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  bool has_session = false;
  int session_result = tos_api_subghz_session_poll(out, &has_session);
  if (has_session)
    return session_result;
  esp_err_t error = acquire_view();
  if (error != ESP_OK) {
    ESP_LOGD(TAG, "Radio view unavailable: %s", esp_err_to_name(error));
    return -error;
  }
  tos_app_ctx_t *ctx = tos_app_ctx_current();
  uint32_t *sequence = ctx != NULL ? &ctx->subghz_sequence : &s_subghz_sequence;
  subghz_rx_result_t result;
  bool has_new = subghz_receiver_get_result(&result) && result.seq != *sequence;
  if (has_new) {
    *sequence = result.seq;
    memcpy(out->protocol, result.name_buf, sizeof(out->protocol));
    out->protocol[sizeof(out->protocol) - 1] = '\0';
    out->decoded = result.decoded;
    out->freq_hz = result.freq;
    out->value = result.data.raw_value;
    out->serial = result.data.serial;
    out->bit_count = result.data.bit_count;
    out->btn = result.data.btn;
  }
  host_local_rf_unlock();
  return has_new ? 1 : 0;
}

static int poll_mesh(tos_lora_msg_t *out) {
  if (out == NULL)
    return -ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  esp_err_t error = acquire_view();
  if (error != ESP_OK) {
    ESP_LOGD(TAG, "Radio view unavailable: %s", esp_err_to_name(error));
    return -error;
  }
  tos_app_ctx_t *ctx = tos_app_ctx_current();
  uint32_t *sequence = ctx != NULL ? &ctx->lora_sequence : &s_lora_sequence;
  lora_msg_t message;
  bool has_new = lora_session_msg_since(sequence, &message, 1) != 0;
  if (has_new) {
    out->outgoing = message.outgoing;
    memcpy(out->who, message.who, sizeof(out->who));
    memcpy(out->text, message.text, sizeof(out->text));
    out->who[sizeof(out->who) - 1] = '\0';
    out->text[sizeof(out->text) - 1] = '\0';
  }
  host_local_rf_unlock();
  return has_new ? 1 : 0;
}

static esp_err_t send_mesh_text(const char *text) {
  if (text == NULL)
    return ESP_ERR_INVALID_ARG;
  if (!tos_app_cap_check(TOS_CAP_RADIO_TX))
    return ESP_ERR_NOT_SUPPORTED;
  if (tos_api_get()->should_stop() || !host_local_rf_is_ready())
    return ESP_ERR_INVALID_STATE;
  if (!host_local_rf_lock(RADIO_VIEW_LOCK_MS))
    return ESP_ERR_TIMEOUT;
  esp_err_t error =
      tos_api_get()->should_stop() ? ESP_ERR_INVALID_STATE : lora_session_send_text(text);
  host_local_rf_unlock();
  return error;
}

static int count_mesh_nodes(void) {
  esp_err_t error = acquire_view();
  if (error != ESP_OK)
    return -error;
  uint16_t count = 0;
  error = lora_session_nodes_snapshot(0, NULL, &count);
  host_local_rf_unlock();
  return error == ESP_OK ? count : -error;
}

static esp_err_t get_mesh_node(int index, tos_lora_node_t *out) {
  if (out == NULL)
    return ESP_ERR_INVALID_ARG;
  memset(out, 0, sizeof(*out));
  if (index < 0 || index > UINT16_MAX)
    return ESP_ERR_INVALID_ARG;
  esp_err_t error = acquire_view();
  if (error != ESP_OK)
    return error;
  lora_node_t node;
  uint16_t count;
  error = lora_session_nodes_snapshot((uint16_t)index, &node, &count);
  if (error == ESP_OK) {
    memcpy(out->name, node.name, sizeof(out->name));
    out->name[sizeof(out->name) - 1] = '\0';
    out->rssi_dbm = node.rssi;
    out->snr_db = node.snr;
  }
  host_local_rf_unlock();
  return error;
}

const tos_subghz_api_t tos_subghz_api_impl = {.spectrum_capture = tos_api_subghz_spectrum_capture,
                                              .rx_start = tos_api_subghz_rx_start,
                                              .set_frequency = tos_api_subghz_set_frequency,
                                              .spectrum_start = tos_api_subghz_spectrum_start,
                                              .spectrum_poll = tos_api_subghz_spectrum_poll,
                                              .session_stop = tos_api_subghz_session_stop,
                                              .rx_poll = poll_subghz,
                                              .get_raw = tos_api_subghz_get_raw,
                                              .replay = tos_api_subghz_replay,
                                              .receive = tos_api_subghz_receive,
                                              .tx_raw = tos_api_subghz_tx_raw};
const tos_lora_api_t tos_lora_api_impl = {.mesh_poll = poll_mesh,
                                          .mesh_send_text = send_mesh_text,
                                          .mesh_node_count = count_mesh_nodes,
                                          .mesh_node_get = get_mesh_node};
