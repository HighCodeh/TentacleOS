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

#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "host_link_apps.h"
#include "host_link_audio.h"
#include "host_link_badusb.h"
#include "host_link_config.h"
#include "host_link_dispatch.h"
#include "host_link_files.h"
#include "host_link_fly.h"
#include "fly_link.h"
#include "host_link_ir.h"
#include "host_link_rf.h"
#include "host_link_local_rf.h"
#include "host_link_led.h"
#include "host_link_lora.h"
#include "host_link_sec.h"
#include "host_link_state.h"
#include "host_link_stream.h"
#include "host_link_subghz.h"
#include "host_link_theme.h"
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
// Host framing is CPU-only and does not require DMA-capable internal RAM.
// Keep both 4 KB staging buffers in PSRAM so USB/BLE transport support does
// not permanently consume the radio/display heap.
EXT_RAM_BSS_ATTR static uint8_t s_acc[HOST_LINK_MAX_FRAME]; // reassembly accumulator
static size_t s_acc_len = 0;
static uint32_t s_tx_counter = 0;
static SemaphoreHandle_t s_lock = NULL;
// Lock order: TX recursive mutex -> security recursive mutex. Neither is ever
// taken by ingress/line-state callbacks. The state spinlock protects only small
// snapshots; no queue, crypto, transport, handler or teardown runs under it.
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_generation = 1;
static uint32_t s_ready_generation = 0;
static uint32_t s_worker_generation = 0;
static uint32_t s_dispatch_generation = 0;
static TaskHandle_t s_worker = NULL;
#define HOST_LINK_RX_CHUNK 64
#define HOST_LINK_RX_DEPTH 64
typedef struct {
  host_link_writer_t writer;
  uint32_t generation;
  uint8_t len;
  bool is_local_rf;
  uint8_t data[HOST_LINK_RX_CHUNK];
} rx_item_t;
static QueueHandle_t s_rx_queue = NULL;
static bool s_initialized = false;
static bool s_started = false;
static void host_link_worker(void *arg);
static void reconcile_session(void);
static void parse_bytes(const uint8_t *data, size_t len);
static void teardown_session(void);

static void process_frame(const uint8_t *frame, size_t total);
static void handle_hello(const uint8_t *payload, uint16_t plen);
static void dispatch_cmd(uint8_t category, uint8_t op, const uint8_t *payload, uint8_t plen);
static uint8_t status_from_err(esp_err_t err);
static void emit_frame(uint32_t generation,
                       uint8_t type,
                       uint8_t category,
                       uint8_t op,
                       const uint8_t *payload,
                       uint16_t payload_len);
static void
send_resp(uint8_t category, uint8_t op, uint8_t status, const uint8_t *data, uint16_t data_len);
static void register_builtins(void);
static uint8_t screen_share_shim(uint16_t cmd,
                                 const uint8_t *payload,
                                 uint16_t plen,
                                 uint8_t *out,
                                 uint16_t out_cap,
                                 uint16_t *out_len);

esp_err_t host_link_init(void) {
  if (s_initialized)
    return ESP_OK;
  if (s_lock == NULL) {
    s_lock = xSemaphoreCreateRecursiveMutex();
    if (s_lock == NULL)
      return ESP_ERR_NO_MEM;
  }
  s_acc_len = 0;
  s_tx_counter = 0;

  esp_err_t err = host_local_rf_init();
  if (err != ESP_OK)
    return err;
  err = host_link_sec_init();
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Security init failed: %s", esp_err_to_name(err));
    return err;
  }

  fly_link_init();
  register_builtins();
  s_initialized = true;
#ifdef HOST_LINK_TEST
  return host_link_start_worker();
#else
  ESP_LOGI(TAG, "Host link initialized; worker deferred until transport use");
  return ESP_OK;
#endif
}

esp_err_t host_link_start_worker(void) {
  if (!s_initialized || s_lock == NULL)
    return ESP_ERR_INVALID_STATE;
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  if (s_started) {
    xSemaphoreGiveRecursive(s_lock);
    return ESP_OK;
  }
  if (s_rx_queue == NULL) {
    s_rx_queue = xQueueCreate(HOST_LINK_RX_DEPTH, sizeof(rx_item_t));
    if (s_rx_queue == NULL) {
      xSemaphoreGiveRecursive(s_lock);
      return ESP_ERR_NO_MEM;
    }
  }
#ifndef HOST_LINK_TEST
  if (s_worker == NULL && xTaskCreatePinnedToCore(host_link_worker,
                                                  "host_link",
                                                  8192,
                                                  NULL,
                                                  SYS_PRIO_SERVICE_HI,
                                                  &s_worker,
                                                  SYS_CORE_RADIO) != pdPASS) {
    xSemaphoreGiveRecursive(s_lock);
    return ESP_ERR_NO_MEM;
  }
#endif
  __atomic_store_n(&s_started, true, __ATOMIC_RELEASE);
  xSemaphoreGiveRecursive(s_lock);
  ESP_LOGI(TAG, "Host link worker started");
  return ESP_OK;
}

bool host_link_session_acquire(host_link_writer_t writer) {
  if (writer == NULL || !__atomic_load_n(&s_started, __ATOMIC_ACQUIRE))
    return false;
  portENTER_CRITICAL(&s_state_lock);
  bool ok = (s_writer == NULL || s_writer == writer);
  if (ok && s_writer != writer) {
    s_writer = writer;
    if (++s_generation == 0)
      ++s_generation;
    s_ready_generation = 0;
  }
  portEXIT_CRITICAL(&s_state_lock);
  if (ok && s_rx_queue != NULL) {
    const rx_item_t wake = {0};
    xQueueSend(s_rx_queue, &wake, 0);
  }
  return ok;
}

static void release_generation(host_link_writer_t writer, uint32_t generation) {
  portENTER_CRITICAL(&s_state_lock);
  bool owned =
      (writer != NULL && s_writer == writer && (generation == 0 || generation == s_generation));
  if (owned) {
    s_writer = NULL;
    if (++s_generation == 0)
      ++s_generation;
    s_ready_generation = 0;
  }
  portEXIT_CRITICAL(&s_state_lock);
  if (owned && s_rx_queue != NULL) {
    const rx_item_t wake = {0};
    xQueueSend(s_rx_queue, &wake, 0);
  }
}

void host_link_session_release(host_link_writer_t writer) {
  release_generation(writer, 0);
}

void host_link_session_release_generation(host_link_writer_t writer, uint32_t generation) {
  if (generation != 0)
    release_generation(writer, generation);
}

bool host_link_session_owns(host_link_writer_t writer) {
  return host_link_session_generation(writer) != 0;
}

void host_link_reset_rx(void) {
  portENTER_CRITICAL(&s_state_lock);
  if (++s_generation == 0)
    ++s_generation;
  s_ready_generation = 0;
  portEXIT_CRITICAL(&s_state_lock);
  if (s_rx_queue != NULL) {
    const rx_item_t wake = {0};
    xQueueSend(s_rx_queue, &wake, 0);
  }
}

uint32_t host_link_session_generation(host_link_writer_t writer) {
  portENTER_CRITICAL(&s_state_lock);
  uint32_t generation = writer != NULL && writer == s_writer ? s_generation : 0;
  portEXIT_CRITICAL(&s_state_lock);
  return generation;
}

bool host_link_generation_matches(host_link_writer_t writer, uint32_t generation) {
  return generation != 0 && host_link_session_generation(writer) == generation;
}

uint32_t host_link_output_generation(void) {
  // A command that was already executing at disconnect must not borrow the
  // next owner's generation for its response or a newly spawned producer.
  if (s_worker != NULL && xTaskGetCurrentTaskHandle() == s_worker)
    return s_dispatch_generation;
  portENTER_CRITICAL(&s_state_lock);
  uint32_t generation = s_ready_generation;
  portEXIT_CRITICAL(&s_state_lock);
  return generation;
}

bool host_link_feed(host_link_writer_t writer,
                    uint32_t generation,
                    const uint8_t *data,
                    size_t len) {
  if (data == NULL || len == 0 || s_rx_queue == NULL ||
      !host_link_generation_matches(writer, generation))
    return false;
  while (len != 0) {
    if (!host_link_generation_matches(writer, generation))
      return false;
    rx_item_t item = {.writer = writer, .generation = generation};
    item.len = len > sizeof(item.data) ? sizeof(item.data) : len;
    memcpy(item.data, data, item.len);
    if (xQueueSend(s_rx_queue, &item, 0) != pdTRUE) {
      // Dropping a middle chunk would splice frames. Fail closed and require
      // a fresh open/HELLO instead; callback never waits for queue space.
      host_link_session_release_generation(writer, generation);
      return false;
    }
    data += item.len;
    len -= item.len;
  }
  return true;
}

static void teardown_session(void) {
  host_stream_teardown();
  lvgl_screen_share_stop();
  host_ir_stop_rx();
  host_subghz_stop();
  host_lora_stop();
  host_rf_stop();
  fly_link_transport_lost();
}

static void reconcile_session(void) {
  for (;;) {
    portENTER_CRITICAL(&s_state_lock);
    uint32_t generation = s_generation;
    portEXIT_CRITICAL(&s_state_lock);
    if (s_worker_generation == generation)
      return;
    // Worker alone owns the accumulator. Wait out any old TX before replacing
    // keys, but release TX before stopping tasks that might themselves emit.
    xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
    host_link_sec_reset();
    s_tx_counter = 0;
    xSemaphoreGiveRecursive(s_lock);
    s_acc_len = 0;
    s_dispatch_generation = 0;
    teardown_session();
    s_worker_generation = generation;
  }
}

esp_err_t host_link_schedule_local_rf(void) {
  if (s_rx_queue == NULL || xTaskGetCurrentTaskHandle() == s_worker)
    return ESP_ERR_INVALID_STATE;
  const rx_item_t item = {.is_local_rf = true};
  return xQueueSend(s_rx_queue, &item, 0) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static bool pump(TickType_t wait) {
  reconcile_session();
  rx_item_t item;
  if (xQueueReceive(s_rx_queue, &item, wait) != pdTRUE)
    return false;
  reconcile_session();
  if (item.is_local_rf) {
    host_local_rf_process();
  } else if (item.len != 0 && host_link_generation_matches(item.writer, item.generation)) {
    s_dispatch_generation = item.generation;
    parse_bytes(item.data, item.len);
    s_dispatch_generation = 0;
  }
  reconcile_session();
  return true;
}

static void host_link_worker(void *arg) {
  (void)arg;
  for (;;)
    pump(pdMS_TO_TICKS(100));
}

#ifdef HOST_LINK_TEST
bool host_link_test_pump(void) {
  s_worker = xTaskGetCurrentTaskHandle();
  return pump(0);
}
#endif

bool host_link_is_emitting(void) {
  return __atomic_load_n(&s_emitting, __ATOMIC_ACQUIRE);
}

static void parse_bytes(const uint8_t *data, size_t len) {
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
    if (s_dispatch_generation == 0)
      return; // HELLO consumed/reset its transcript

    portENTER_CRITICAL(&s_state_lock);
    bool still_current = s_generation == s_worker_generation;
    portEXIT_CRITICAL(&s_state_lock);
    if (!still_current)
      return; // reconcile owns reset; no post-dispatch underflow

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

  if (ver != HOST_LINK_VER || (flags & ~HOST_LINK_FLAG_AUTH) != 0) {
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
    if (flags != 0)
      return;
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
  portENTER_CRITICAL(&s_state_lock);
  bool current = s_dispatch_generation == s_generation && s_ready_generation == s_generation;
  portEXIT_CRITICAL(&s_state_lock);
  if (!current)
    return;

  // RF captures use request objects; other local handlers use the registry.
  // Unclaimed commands relay to the C5. Registered handlers may exceed the 255-byte relay cap
  // (file/IR/Sub-GHz chunks), so the cap only guards the relay path below.
  uint16_t cmd = SPI_CMD(category, op);
  if (category == SPI_CAT_NFC || category == SPI_CAT_RFID) {
    uint8_t response[HOST_RF_RESPONSE_MAX];
    host_rf_request_t request = {.command = cmd,
                                 .payload = payload,
                                 .length = plen16,
                                 .out = response,
                                 .capacity = sizeof(response)};
    uint8_t status = host_rf_handle(&request);
    send_resp(category, op, status, response, request.out_length);
    return;
  }
  host_link_handler_t h;
  if (host_link_dispatch_lookup(cmd, &h)) {
    // The single companion session serializes dispatch, so one shared scratch
    // buffer sized to the largest response (a file chunk) is safe.
    static uint8_t scratch[HOST_FILE_CHUNK];
    uint16_t rlen = 0;
    uint8_t status = h.handle(cmd, payload, plen16, scratch, h.out_cap, &rlen);
    send_resp(category, op, status, scratch, rlen);
    return;
  }

  if (plen16 > SPI_MAX_PAYLOAD) {
    send_resp(category, op, SPI_STATUS_INVALID_ARG, NULL, 0);
    return;
  }
  dispatch_cmd(category, op, payload, (uint8_t)plen16);
}

static void handle_hello(const uint8_t *payload, uint16_t plen) {
  if (payload == NULL || plen != 1 + HOST_LINK_NONCE_SIZE || payload[0] != HOST_LINK_VER)
    return;
  // Rekey is also a generation boundary. Invalidate queued input and every old
  // producer before deriving; do not let an old log precede the new ACK.
  portENTER_CRITICAL(&s_state_lock);
  bool current = s_generation == s_dispatch_generation && s_writer != NULL;
  if (current) {
    if (++s_generation == 0)
      ++s_generation;
    s_ready_generation = 0;
  }
  uint32_t generation = s_generation;
  portEXIT_CRITICAL(&s_state_lock);
  if (!current)
    return;
  // No TX lock held during teardown (producers may be finishing a write).
  teardown_session();
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  host_link_sec_reset();
  s_tx_counter = 0;
  uint8_t ack[1 + HOST_LINK_NONCE_SIZE + HOST_LINK_DEVICE_ID_SIZE + HOST_LINK_MAC_SIZE];
  size_t ack_len = 0;
  esp_err_t err = host_link_sec_handle_hello(payload, plen, ack, sizeof(ack), &ack_len);
  if (err != ESP_OK) {
    xSemaphoreGiveRecursive(s_lock);
    ESP_LOGW(TAG, "HELLO rejected: %s", esp_err_to_name(err));
    return;
  }

  // HELLO_ACK travels unauthenticated (its proof is mac_psk in the payload).
  emit_frame(generation, HOST_TYPE_HELLO_ACK, 0x00, 0x00, ack, (uint16_t)ack_len);
  portENTER_CRITICAL(&s_state_lock);
  if (s_generation == generation && s_writer != NULL)
    s_ready_generation = generation;
  portEXIT_CRITICAL(&s_state_lock);
  xSemaphoreGiveRecursive(s_lock);
  // Discard trailing bytes from the previous transcript. The next ingress
  // chunk is tagged by the transport with the new generation.
  s_acc_len = 0;
  s_worker_generation = generation;
  s_dispatch_generation = 0;
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
static void emit_frame(uint32_t generation,
                       uint8_t type,
                       uint8_t category,
                       uint8_t op,
                       const uint8_t *payload,
                       uint16_t payload_len) {
  if (generation == 0 || s_lock == NULL)
    return;

  EXT_RAM_BSS_ATTR static uint8_t frame[HOST_LINK_MAX_FRAME];
  if (payload_len >
      HOST_LINK_MAX_FRAME - HOST_LINK_HDR_SIZE - HOST_LINK_BODY_HDR - HOST_LINK_MAC_SIZE)
    return;
  uint16_t body_len = (uint16_t)(HOST_LINK_BODY_HDR + payload_len);

  // Handshake frames are always unauthenticated; everything else carries a MAC
  // once the session is up.
  bool is_handshake = (type == HOST_TYPE_HELLO || type == HOST_TYPE_HELLO_ACK);
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  portENTER_CRITICAL(&s_state_lock);
  host_link_writer_t writer = s_writer;
  bool current = writer != NULL && generation == s_generation &&
                 (is_handshake || generation == s_ready_generation);
  portEXIT_CRITICAL(&s_state_lock);
  bool authed = !is_handshake && host_link_sec_is_authenticated();
  if (!current || (!is_handshake && !authed)) {
    xSemaphoreGiveRecursive(s_lock);
    return;
  }
  size_t span = (size_t)HOST_LINK_HDR_SIZE + body_len;
  size_t out_len = span + (authed ? HOST_LINK_MAC_SIZE : 0);
  if (out_len > sizeof(frame)) {
    xSemaphoreGiveRecursive(s_lock);
    return;
  }
  if (s_tx_counter == UINT32_MAX) {
    xSemaphoreGiveRecursive(s_lock);
    host_link_session_release_generation(writer, generation);
    return;
  }
  __atomic_store_n(&s_emitting, true, __ATOMIC_RELEASE); // gate recursive log tee
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
  if (authed && !host_link_sec_sign_outbound(frame + 2, span - 2, frame + span)) {
    __atomic_store_n(&s_emitting, false, __ATOMIC_RELEASE);
    xSemaphoreGiveRecursive(s_lock);
    return;
  }

  if (host_link_generation_matches(writer, generation))
    writer(generation, frame, out_len);
  __atomic_store_n(&s_emitting, false, __ATOMIC_RELEASE);
  xSemaphoreGiveRecursive(s_lock);
}

static void
send_resp(uint8_t category, uint8_t op, uint8_t status, const uint8_t *data, uint16_t data_len) {
  // [status][data...]. Sized for the largest local response (a file chunk).
  // Single-session guarantees only one dispatcher runs at a time.
  static uint8_t payload[1 + HOST_FILE_CHUNK];
  if (data_len > HOST_FILE_CHUNK)
    data_len = HOST_FILE_CHUNK;
  payload[0] = status;
  if (data_len > 0 && data != NULL)
    memcpy(payload + 1, data, data_len);
  emit_frame(
      s_dispatch_generation, HOST_TYPE_RESP, category, op, payload, (uint16_t)(1 + data_len));
}

// screen share exposes size_t out_cap; adapt it to the registry's uint16_t.
static uint8_t screen_share_shim(uint16_t cmd,
                                 const uint8_t *payload,
                                 uint16_t plen,
                                 uint8_t *out,
                                 uint16_t out_cap,
                                 uint16_t *out_len) {
  return lvgl_screen_share_handle(cmd, payload, plen, out, out_cap, out_len);
}

static void register_builtins(void) {
  static bool done = false;
  if (done)
    return;
  done = true;

  const host_link_handler_t builtins[] = {
      {SPI_CAT_SYSTEM,
       SPI_ID_FILE_LIST & 0xFF,
       SPI_ID_FILE_MKDIR & 0xFF,
       HOST_FILE_CHUNK,
       host_files_handle,
       "files"},
      {SPI_CAT_SYSTEM,
       SPI_ID_SYSTEM_DEVICE_STATE & 0xFF,
       SPI_ID_SYSTEM_SET_SETTINGS & 0xFF,
       HOST_FILE_DATA_MAX,
       host_state_handle,
       "state"},
      {SPI_CAT_SYSTEM,
       SPI_ID_SYSTEM_CONFIG_GET & 0xFF,
       SPI_ID_SYSTEM_CONFIG_GET & 0xFF,
       HOST_FILE_DATA_MAX,
       host_config_handle,
       "config-get"},
      {SPI_CAT_SYSTEM,
       SPI_ID_SYSTEM_SET_THEME & 0xFF,
       SPI_ID_SYSTEM_SET_THEME & 0xFF,
       HOST_FILE_DATA_MAX,
       host_theme_handle,
       "theme"},
      {SPI_CAT_SYSTEM,
       SPI_ID_SYSTEM_CONFIG_SET & 0xFF,
       SPI_ID_SYSTEM_CONFIG_SET & 0xFF,
       HOST_FILE_DATA_MAX,
       host_config_handle,
       "config-set"},
      {SPI_CAT_SYSTEM,
       SPI_ID_APP_LIST & 0xFF,
       SPI_ID_APP_GRANT & 0xFF,
       HOST_FILE_DATA_MAX,
       host_apps_handle,
       "apps"},
      {SPI_CAT_SESSION, 0x00, 0xFF, 8, host_stream_session_ctrl, "session"},
      {SPI_CAT_WIFI,
       SPI_ID_WIFI_APP_SNIFFER & 0xFF,
       SPI_ID_WIFI_APP_SNIFFER & 0xFF,
       8,
       host_stream_start,
       "wifi-sniffer"},
      {SPI_CAT_SCREEN, 0x00, 0xFF, 8, screen_share_shim, "screen"},
      {SPI_CAT_IR, 0x00, 0xFF, 8, host_ir_handle, "ir"},
      {SPI_CAT_SUBGHZ, 0x00, 0xFF, HOST_FILE_DATA_MAX, host_subghz_handle, "subghz"},
      {SPI_CAT_AUDIO, 0x00, 0xFF, 8, host_audio_handle, "audio"},
      {SPI_CAT_LED, 0x00, 0xFF, 8, host_led_handle, "led"},
      {SPI_CAT_BADUSB, 0x00, 0xFF, HOST_FILE_DATA_MAX, host_badusb_handle, "badusb"},
      {SPI_CAT_LORACFG, 0x00, 0xFF, HOST_FILE_DATA_MAX, host_lora_handle, "loracfg"},
      {SPI_CAT_LORACHAT, 0x00, 0xFF, HOST_FILE_DATA_MAX, host_lora_handle, "lorachat"},
      {SPI_CAT_FLY, 0x00, 0xFF, FLY_LINK_RESPONSE_MAX, host_fly_handle, "fly"},
  };
  for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
    esp_err_t err = host_link_register(&builtins[i]);
    if (err != ESP_OK)
      ESP_LOGE(TAG, "register builtin '%s' failed: %s", builtins[i].name, esp_err_to_name(err));
  }
}

void host_link_mark_ble_writer(host_link_writer_t writer) {
  portENTER_CRITICAL(&s_state_lock);
  s_ble_writer = writer;
  portEXIT_CRITICAL(&s_state_lock);
}

// Emit one LOG frame. Background logs (gate_ble=true) are suppressed over BLE
// when the log-over-BLE toggle is off; USB always carries them, and console
// output (gate_ble=false) is always delivered.
static void emit_log_frame(uint32_t generation,
                           host_log_source_t source,
                           host_log_level_t level,
                           const char *text,
                           size_t text_len,
                           bool gate_ble) {
  if (text == NULL)
    return;
  portENTER_CRITICAL(&s_state_lock);
  bool ble = s_writer != NULL && s_writer == s_ble_writer;
  portEXIT_CRITICAL(&s_state_lock);
  if (gate_ble && ble && !host_settings_log_over_ble_enabled()) {
    return;
  }

  if (text_len > HOST_LINK_LOG_TEXT_MAX)
    text_len = HOST_LINK_LOG_TEXT_MAX;

  uint8_t payload[2 + HOST_LINK_LOG_TEXT_MAX]; // [source][level][text...]
  payload[0] = (uint8_t)source;
  payload[1] = (uint8_t)level;
  memcpy(payload + 2, text, text_len);
  emit_frame(generation, HOST_TYPE_LOG, 0x00, 0x00, payload, (uint16_t)(2 + text_len));
}

void host_link_emit_log(host_log_source_t source,
                        host_log_level_t level,
                        const char *text,
                        size_t text_len) {
  emit_log_frame(host_link_output_generation(), source, level, text, text_len, true);
}

void host_link_emit_console(const char *text, size_t text_len) {
  emit_log_frame(
      host_link_output_generation(), HOST_LOG_SRC_P4, HOST_LOG_LEVEL_INFO, text, text_len, false);
}

void host_link_emit_stream(uint8_t category, uint8_t op, const uint8_t *data, uint16_t len) {
  emit_frame(host_link_output_generation(), HOST_TYPE_STREAM, category, op, data, len);
}

void host_link_emit_stream_generation(
    uint32_t generation, uint8_t category, uint8_t op, const uint8_t *data, uint16_t len) {
  emit_frame(generation, HOST_TYPE_STREAM, category, op, data, len);
}

void host_link_emit_log_generation(uint32_t generation,
                                   host_log_source_t source,
                                   host_log_level_t level,
                                   const char *text,
                                   size_t text_len) {
  emit_log_frame(generation, source, level, text, text_len, true);
}
