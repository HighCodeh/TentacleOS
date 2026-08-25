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

#include "host_link.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "host_link_audio.h"
#include "host_link_files.h"
#include "host_link_ir.h"
#include "host_link_led.h"
#include "host_link_sec.h"
#include "host_link_state.h"
#include "host_link_stream.h"
#include "host_link_subghz.h"
#include "lvgl_screen_share.h"
#include "spi_bridge.h"
#include "spi_protocol.h"
#include "spi_timeouts.h"

static const char *TAG = "HOST_LINK";

// Largest host frame we accept/emit. Commands are small; file-write chunks and
// stream batches are the big ones — keep some headroom.
#define HOST_LINK_MAX_FRAME    4096
#define HOST_LINK_BODY_HDR     3   // type + category + op
#define HOST_LINK_LOG_TEXT_MAX 240 // per-LOG-frame text cap (keeps the payload small)

static host_link_writer_t s_writer = NULL;
static host_link_writer_t s_ble_writer = NULL; // identifies the BLE transport
static volatile bool s_emitting = false;       // true while a frame is being written out
static uint8_t s_acc[HOST_LINK_MAX_FRAME];     // reassembly accumulator
static size_t s_acc_len = 0;
static uint32_t s_tx_counter = 0;
static SemaphoreHandle_t s_lock = NULL;

static void process_frame(const uint8_t *frame, size_t total);
static void handle_hello(const uint8_t *payload, uint16_t plen);
static void dispatch_cmd(uint8_t category, uint8_t op, const uint8_t *payload, uint8_t plen);
static uint8_t status_from_err(esp_err_t err);
static void emit_frame(
    uint8_t type, uint8_t category, uint8_t op, const uint8_t *payload, uint16_t payload_len);
static void
send_resp(uint8_t category, uint8_t op, uint8_t status, const uint8_t *data, uint16_t data_len);

esp_err_t host_link_init(void) {
  if (s_lock == NULL) {
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL)
      return ESP_ERR_NO_MEM;
  }
  s_acc_len = 0;
  s_tx_counter = 0;

  esp_err_t err = host_link_sec_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Security init failed: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "Host link initialized");
  return ESP_OK;
}

bool host_link_session_acquire(host_link_writer_t writer) {
  if (writer == NULL)
    return false;
  xSemaphoreTake(s_lock, portMAX_DELAY);
  bool ok = (s_writer == NULL || s_writer == writer);
  if (ok)
    s_writer = writer;
  xSemaphoreGive(s_lock);
  return ok;
}

void host_link_session_release(host_link_writer_t writer) {
  xSemaphoreTake(s_lock, portMAX_DELAY);
  bool owned = (s_writer == writer);
  if (owned) {
    s_writer = NULL;
    s_acc_len = 0;
  }
  xSemaphoreGive(s_lock);
  if (owned) {
    host_stream_teardown();   // stop any live stream so the C5 session is reaped
    lvgl_screen_share_stop(); // safeguard: never keep capturing into a dead link
    host_ir_stop_rx();        // stop IR capture so it can't stream into a dead link
    host_subghz_stop();       // stop Sub-GHz rx/spectrum streams too
    host_link_sec_reset();    // force re-handshake on the next session
  }
}

bool host_link_session_owns(host_link_writer_t writer) {
  return s_writer == writer; // single-word read; benign race
}

void host_link_reset_rx(void) {
  s_acc_len = 0;
}

bool host_link_is_emitting(void) {
  return s_emitting;
}

void host_link_feed(const uint8_t *data, size_t len) {
  if (data == NULL || len == 0)
    return;

  for (size_t i = 0; i < len; i++) {
    if (s_acc_len < sizeof(s_acc)) {
      s_acc[s_acc_len++] = data[i];
    } else {
      // Overflow: drop the oldest half to recover sync rather than wedging.
      memmove(s_acc, s_acc + sizeof(s_acc) / 2, sizeof(s_acc) / 2);
      s_acc_len = sizeof(s_acc) / 2;
      s_acc[s_acc_len++] = data[i];
    }
  }

  // Parse as many complete frames as the accumulator holds.
  for (;;) {
    // Resync to MAGIC.
    if (s_acc_len >= 1 && s_acc[0] != HOST_LINK_MAGIC0) {
      size_t drop = 1;
      while (drop < s_acc_len && s_acc[drop] != HOST_LINK_MAGIC0)
        drop++;
      memmove(s_acc, s_acc + drop, s_acc_len - drop);
      s_acc_len -= drop;
    }
    if (s_acc_len < HOST_LINK_HDR_SIZE)
      return;
    if (s_acc[1] != HOST_LINK_MAGIC1) {
      // Second magic byte wrong — drop the first and retry.
      memmove(s_acc, s_acc + 1, s_acc_len - 1);
      s_acc_len -= 1;
      continue;
    }

    uint8_t flags = s_acc[3];
    uint16_t body_len = (uint16_t)s_acc[8] | ((uint16_t)s_acc[9] << 8);
    size_t mac = (flags & HOST_LINK_FLAG_AUTH) ? HOST_LINK_MAC_SIZE : 0;
    size_t total = HOST_LINK_HDR_SIZE + body_len + mac;

    if (body_len + HOST_LINK_HDR_SIZE + mac > sizeof(s_acc)) {
      // Bogus oversized length — drop the magic byte and resync.
      memmove(s_acc, s_acc + 1, s_acc_len - 1);
      s_acc_len -= 1;
      continue;
    }
    if (s_acc_len < total)
      return; // wait for the rest

    process_frame(s_acc, total);

    memmove(s_acc, s_acc + total, s_acc_len - total);
    s_acc_len -= total;
  }
}

// Static functions

static void process_frame(const uint8_t *frame, size_t total) {
  uint8_t ver = frame[2];
  uint8_t flags = frame[3];
  uint32_t counter = (uint32_t)frame[4] | ((uint32_t)frame[5] << 8) | ((uint32_t)frame[6] << 16) |
                     ((uint32_t)frame[7] << 24);
  uint16_t body_len = (uint16_t)frame[8] | ((uint16_t)frame[9] << 8);
  (void)total;

  if (ver != HOST_LINK_VER) {
    ESP_LOGW(TAG, "Unsupported host-link version %u", ver);
    return;
  }
  if (body_len < HOST_LINK_BODY_HDR)
    return;

  const uint8_t *body = frame + HOST_LINK_HDR_SIZE;
  uint8_t type = body[0];
  uint8_t category = body[1];
  uint8_t op = body[2];
  const uint8_t *payload = body + HOST_LINK_BODY_HDR;
  uint16_t plen16 = body_len - HOST_LINK_BODY_HDR;

  // Pre-auth handshake: the only frame accepted before keys exist.
  if (type == HOST_TYPE_HELLO) {
    handle_hello(payload, plen16);
    return;
  }

  // Every other inbound frame must be authenticated: MAC-valid and fresh. The
  // MAC covers [VER .. end of BODY); the 16-byte MAC follows the body.
  if (!(flags & HOST_LINK_FLAG_AUTH) || !host_link_sec_is_authenticated()) {
    ESP_LOGW(TAG, "Dropping unauthenticated frame type 0x%02X", type);
    return;
  }
  const uint8_t *mac = frame + HOST_LINK_HDR_SIZE + body_len;
  size_t span_len = (size_t)(HOST_LINK_HDR_SIZE + body_len) - 2; // from VER (offset 2)
  if (!host_link_sec_verify_inbound(frame + 2, span_len, mac, counter)) {
    ESP_LOGW(TAG, "Dropping frame with bad MAC/counter (type 0x%02X)", type);
    return;
  }

  if (type != HOST_TYPE_CMD) {
    ESP_LOGW(TAG, "Ignoring non-CMD frame type 0x%02X from host", type);
    return;
  }

  // File ops are handled locally on the P4 (it owns flash + SD) and may carry
  // payloads larger than one SPI frame, so they bypass the relay size cap.
  uint16_t cmd = SPI_CMD(category, op);
  if (host_files_is_file_op(cmd)) {
    static uint8_t fdata[HOST_FILE_DATA_MAX];
    uint16_t flen = 0;
    uint8_t status = host_files_handle(cmd, payload, plen16, fdata, sizeof(fdata), &flen);
    send_resp(category, op, status, fdata, flen);
    return;
  }

  // Device state, settings, and console exec are also handled locally on the P4.
  if (host_state_is_local_op(cmd)) {
    static uint8_t sdata[HOST_FILE_DATA_MAX];
    uint16_t slen = 0;
    uint8_t status = host_state_handle(cmd, payload, plen16, sdata, sizeof(sdata), &slen);
    send_resp(category, op, status, sdata, slen);
    return;
  }

  // SESSION control (heartbeat/stop) is the companion's liveness proxy — handled
  // locally, never relayed (the P4 keeps heartbeating the C5 on its own).
  if (category == SPI_CAT_SESSION) {
    uint8_t cdata[8];
    uint16_t clen = 0;
    uint8_t status = host_stream_session_ctrl(cmd, payload, plen16, cdata, sizeof(cdata), &clen);
    send_resp(category, op, status, cdata, clen);
    return;
  }

  // Session-based streaming ops (e.g. sniffer) run through the spi_session model
  // and push records to the app as STREAM frames.
  if (host_stream_is_session_op(cmd)) {
    uint8_t sdata[8];
    uint16_t slen = 0;
    uint8_t status = host_stream_start(cmd, payload, plen16, sdata, sizeof(sdata), &slen);
    send_resp(category, op, status, sdata, slen);
    return;
  }

  // Screen sharing is P4-native (snapshot + USB stream), handled locally.
  if (lvgl_screen_share_is_host_op(cmd)) {
    uint8_t sdata[8];
    uint16_t slen = 0;
    uint8_t status = lvgl_screen_share_handle(cmd, payload, plen16, sdata, sizeof(sdata), &slen);
    send_resp(category, op, status, sdata, slen);
    return;
  }

  // IR is P4-native (RMT); handled locally. IR_TX_RAW can carry more than one SPI
  // frame, so like file ops it must run before the relay size cap below.
  if (host_ir_is_op(cmd)) {
    uint8_t idata[8];
    uint16_t ilen = 0;
    uint8_t status = host_ir_handle(cmd, payload, plen16, idata, sizeof(idata), &ilen);
    send_resp(category, op, status, idata, ilen);
    return;
  }

  // LED and audio are P4-native, handled locally (tiny responses).
  if (host_led_is_op(cmd)) {
    uint8_t d[8];
    uint16_t l = 0;
    uint8_t status = host_led_handle(cmd, payload, plen16, d, sizeof(d), &l);
    send_resp(category, op, status, d, l);
    return;
  }
  if (host_audio_is_op(cmd)) {
    uint8_t d[8];
    uint16_t l = 0;
    uint8_t status = host_audio_handle(cmd, payload, plen16, d, sizeof(d), &l);
    send_resp(category, op, status, d, l);
    return;
  }

  // Sub-GHz is P4-native (CC1101); handled locally. TX_RAW carries many timings
  // and LIST returns rows inline (no data pipe on the P4 side), so both need the
  // larger buffer and must run before the relay cap.
  if (host_subghz_is_op(cmd)) {
    static uint8_t sgdata[HOST_FILE_DATA_MAX];
    uint16_t sglen = 0;
    uint8_t status = host_subghz_handle(cmd, payload, plen16, sgdata, sizeof(sgdata), &sglen);
    send_resp(category, op, status, sgdata, sglen);
    return;
  }

  // Everything else relays to the C5 over SPI, whose payloads cap at one frame.
  if (plen16 > SPI_MAX_PAYLOAD) {
    send_resp(category, op, SPI_STATUS_INVALID_ARG, NULL, 0);
    return;
  }

  dispatch_cmd(category, op, payload, (uint8_t)plen16);
}

static void handle_hello(const uint8_t *payload, uint16_t plen) {
  uint8_t ack[1 + HOST_LINK_NONCE_SIZE + HOST_LINK_DEVICE_ID_SIZE + HOST_LINK_MAC_SIZE];
  size_t ack_len = 0;
  esp_err_t err = host_link_sec_handle_hello(payload, plen, ack, sizeof(ack), &ack_len);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "HELLO rejected: %s", esp_err_to_name(err));
    return;
  }

  // The handshake reset the session; restart the outbound counter so the first
  // authenticated device→app frame begins a fresh sequence.
  xSemaphoreTake(s_lock, portMAX_DELAY);
  s_tx_counter = 0;
  xSemaphoreGive(s_lock);

  // HELLO_ACK travels unauthenticated (its proof is mac_psk in the payload).
  emit_frame(HOST_TYPE_HELLO_ACK, 0x00, 0x00, ack, (uint16_t)ack_len);
}

static void dispatch_cmd(uint8_t category, uint8_t op, const uint8_t *payload, uint8_t plen) {
  // Phase 1: route every command through the existing SPI bridge HAL. Local P4
  // handlers (file ops, device state) are added in later phases.
  uint16_t cmd = SPI_CMD(category, op);
  spi_header_t resp_hdr = {0};
  uint8_t resp_buf[SPI_MAX_PAYLOAD];

  esp_err_t ret = spi_bridge_send_command(
      cmd, payload, plen, &resp_hdr, resp_buf, sizeof(resp_buf), spi_bridge_get_timeout(cmd));

  uint8_t status = status_from_err(ret);
  uint8_t data_len = (ret == ESP_OK) ? resp_hdr.length : 0;
  send_resp(category, op, status, resp_buf, data_len);
}

static uint8_t status_from_err(esp_err_t err) {
  switch (err) {
    case ESP_OK:
      return SPI_STATUS_OK;
    case ESP_ERR_INVALID_STATE:
      return SPI_STATUS_BUSY;
    case ESP_ERR_NOT_SUPPORTED:
      return SPI_STATUS_UNSUPPORTED;
    case ESP_ERR_INVALID_ARG:
      return SPI_STATUS_INVALID_ARG;
    default:
      return SPI_STATUS_ERROR;
  }
}

// Assemble and write one host frame. Holds s_lock across the counter bump and
// the transport write so frames stay atomic and the per-direction counter stays
// monotonic even when RESP (command worker) and LOG (log worker) race.
static void emit_frame(
    uint8_t type, uint8_t category, uint8_t op, const uint8_t *payload, uint16_t payload_len) {
  if (s_writer == NULL)
    return;

  static uint8_t frame[HOST_LINK_MAX_FRAME];
  uint16_t body_len = (uint16_t)(HOST_LINK_BODY_HDR + payload_len);

  // Handshake frames are always unauthenticated; everything else carries a MAC
  // once the session is up.
  bool is_handshake = (type == HOST_TYPE_HELLO || type == HOST_TYPE_HELLO_ACK);
  bool authed = !is_handshake && host_link_sec_is_authenticated();
  size_t span = (size_t)HOST_LINK_HDR_SIZE + body_len;
  size_t out_len = span + (authed ? HOST_LINK_MAC_SIZE : 0);
  if (out_len > sizeof(frame))
    return; // never overflow the frame buffer

  xSemaphoreTake(s_lock, portMAX_DELAY);
  // Re-read the writer under the lock: a transport disconnect (session_release)
  // can null it between the top-of-function check and the call below, which
  // would turn the indirect call into a jump to NULL.
  host_link_writer_t writer = s_writer;
  if (writer == NULL) {
    xSemaphoreGive(s_lock);
    return;
  }
  s_emitting = true; // the writer runs blocking SPI that may log; gate the tee
  uint32_t counter = s_tx_counter++;

  frame[0] = HOST_LINK_MAGIC0;
  frame[1] = HOST_LINK_MAGIC1;
  frame[2] = HOST_LINK_VER;
  frame[3] = authed ? HOST_LINK_FLAG_AUTH : 0x00;
  frame[4] = (uint8_t)(counter & 0xFF);
  frame[5] = (uint8_t)((counter >> 8) & 0xFF);
  frame[6] = (uint8_t)((counter >> 16) & 0xFF);
  frame[7] = (uint8_t)((counter >> 24) & 0xFF);
  frame[8] = (uint8_t)(body_len & 0xFF);
  frame[9] = (uint8_t)((body_len >> 8) & 0xFF);
  frame[HOST_LINK_HDR_SIZE + 0] = type;
  frame[HOST_LINK_HDR_SIZE + 1] = category;
  frame[HOST_LINK_HDR_SIZE + 2] = op;
  if (payload_len > 0 && payload != NULL)
    memcpy(frame + HOST_LINK_HDR_SIZE + HOST_LINK_BODY_HDR, payload, payload_len);

  // MAC covers [VER .. end of BODY) and is appended after the body.
  if (authed)
    host_link_sec_sign_outbound(frame + 2, span - 2, frame + span);

  writer(frame, out_len);
  s_emitting = false;
  xSemaphoreGive(s_lock);
}

static void
send_resp(uint8_t category, uint8_t op, uint8_t status, const uint8_t *data, uint16_t data_len) {
  // [status][data...]. Sized for the largest local response (a file chunk).
  // Single-session guarantees only one dispatcher runs at a time.
  static uint8_t payload[1 + HOST_FILE_DATA_MAX];
  if (data_len > HOST_FILE_DATA_MAX)
    data_len = HOST_FILE_DATA_MAX;
  payload[0] = status;
  if (data_len > 0 && data != NULL)
    memcpy(payload + 1, data, data_len);
  emit_frame(HOST_TYPE_RESP, category, op, payload, (uint16_t)(1 + data_len));
}

void host_link_mark_ble_writer(host_link_writer_t writer) {
  s_ble_writer = writer;
}

// Emit one LOG frame. Background logs (gate_ble=true) are suppressed over BLE
// when the log-over-BLE toggle is off; USB always carries them, and console
// output (gate_ble=false) is always delivered.
static void emit_log_frame(host_log_source_t source,
                           host_log_level_t level,
                           const char *text,
                           size_t text_len,
                           bool gate_ble) {
  if (text == NULL)
    return;
  if (gate_ble && s_writer != NULL && s_writer == s_ble_writer &&
      !host_settings_log_over_ble_enabled()) {
    return;
  }

  if (text_len > HOST_LINK_LOG_TEXT_MAX)
    text_len = HOST_LINK_LOG_TEXT_MAX;

  uint8_t payload[2 + HOST_LINK_LOG_TEXT_MAX]; // [source][level][text...]
  payload[0] = (uint8_t)source;
  payload[1] = (uint8_t)level;
  memcpy(payload + 2, text, text_len);
  emit_frame(HOST_TYPE_LOG, 0x00, 0x00, payload, (uint16_t)(2 + text_len));
}

void host_link_emit_log(host_log_source_t source,
                        host_log_level_t level,
                        const char *text,
                        size_t text_len) {
  emit_log_frame(source, level, text, text_len, true);
}

void host_link_emit_console(const char *text, size_t text_len) {
  emit_log_frame(HOST_LOG_SRC_P4, HOST_LOG_LEVEL_INFO, text, text_len, false);
}

void host_link_emit_stream(uint8_t category, uint8_t op, const uint8_t *data, uint16_t len) {
  emit_frame(HOST_TYPE_STREAM, category, op, data, len);
}
