// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fly_link.h"

#include <string.h>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "spi_protocol.h"

#define FLY_MS_TO_US        1000U
#define FLY_MAX_RATE_HZ     20U
#define FLY_FLAG_OPEN       1U
#define FLY_FLAG_FRESH      2U
#define FLY_FLAG_OVERFLOW   4U
#define FLY_ANIMATION_COUNT 4U
#define FLY_STIMULUS_SWEET  1U
#define FLY_STIMULUS_TOUCH  2U
#define FLY_U16_BYTES       2U
#define FLY_U32_BYTES       4U
#define FLY_BYTE_SHIFT      8U
#define FLY_SEQUENCE_OFFSET 8U
#define FLY_TIME_OFFSET     12U
#define FLY_ACK_OFFSET      16U
#define FLY_ACTIVITY_OFFSET 28U
#define FLY_STIMULUS_OFFSET 40U
#define FLY_STIMULUS_BYTES  6U

typedef struct {
  uint32_t sequence;
  uint16_t x;
  uint16_t y;
  uint16_t value;
  uint8_t action;
} fly_link_event_t;

typedef struct {
  fly_link_snapshot_t snapshot;
  fly_link_event_t events[FLY_LINK_QUEUE_SIZE];
  uint8_t config[FLY_LINK_CONFIG_SIZE];
  uint8_t raw[FLY_LINK_SNAPSHOT_SIZE];
  uint64_t received_ms;
  uint64_t frame_received_ms;
  uint32_t frame_sequence;
  uint32_t generation;
  uint32_t session;
  uint32_t ended_session;
  uint32_t sequence;
  uint32_t generated;
  uint32_t sent;
  uint32_t acknowledged;
  uint8_t head;
  uint8_t count;
  bool is_open;
  bool has_snapshot;
  bool has_overflow;
  bool has_frame;
  bool frame_error;
} fly_link_state_t;

static const char *TAG = "FLY_LINK";
// Static mailbox: critical sections only copy bounded data, never call UI/I/O.
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static fly_link_state_t s_state;
// Lifecycle/large pixels use a task mutex, never the state spinlock. Order is
// frame mutex -> state portMUX. UI displays its own third buffer, so the worker
// can swap front/back without LVGL retaining a pointer into either allocation.
static SemaphoreHandle_t s_frame_lock;
static uint8_t *s_frame_front;
static uint8_t *s_frame_back;
static uint32_t s_frame_started;
static uint32_t s_frame_next;
static bool s_frame_pending;

static uint16_t read_u16(const uint8_t *data);
static uint32_t read_u32(const uint8_t *data);
static void write_u16(uint8_t *out, uint16_t value);
static void write_u32(uint8_t *out, uint32_t value);
static uint64_t now_ms(void);
static bool is_fresh(uint64_t now);
static void reset_state(bool is_open);
static bool config_valid(const uint8_t *payload);
static bool decode_snapshot(const uint8_t *payload, fly_link_snapshot_t *out_snapshot);
static uint8_t configure(const uint8_t *payload);
static uint8_t exchange(const uint8_t *payload, uint64_t now);
static void encode_response(uint8_t *out, uint16_t *out_len, bool with_events, uint64_t now);
static void frame_reset(bool release_storage);
static uint8_t frame_handle(const uint8_t *payload, uint16_t plen, uint8_t *out, uint16_t *out_len);

void fly_link_init(void) {
  if (s_frame_lock == NULL)
    s_frame_lock = xSemaphoreCreateMutex();
  if (s_frame_lock == NULL) {
    ESP_LOGE(TAG, "Mosca frame mutex unavailable");
    return;
  }
  xSemaphoreTake(s_frame_lock, portMAX_DELAY);
  frame_reset(true);
  portENTER_CRITICAL(&s_lock);
  reset_state(false);
  portEXIT_CRITICAL(&s_lock);
  xSemaphoreGive(s_frame_lock);
  ESP_LOGI(TAG, "Mosca TEST link ready");
}

esp_err_t fly_link_open(void) {
  if (s_frame_lock == NULL)
    fly_link_init();
  if (s_frame_lock == NULL)
    return ESP_ERR_NO_MEM;
  xSemaphoreTake(s_frame_lock, portMAX_DELAY);
  frame_reset(true);
  portENTER_CRITICAL(&s_lock);
  reset_state(true);
  portEXIT_CRITICAL(&s_lock);
  xSemaphoreGive(s_frame_lock);
  return ESP_OK;
}

void fly_link_close(void) {
  if (s_frame_lock == NULL)
    return;
  xSemaphoreTake(s_frame_lock, portMAX_DELAY);
  portENTER_CRITICAL(&s_lock);
  reset_state(false);
  portEXIT_CRITICAL(&s_lock);
  frame_reset(true);
  xSemaphoreGive(s_frame_lock);
}

void fly_link_transport_lost(void) {
  if (s_frame_lock == NULL)
    return;
  xSemaphoreTake(s_frame_lock, portMAX_DELAY);
  portENTER_CRITICAL(&s_lock);
  if (s_state.session != 0)
    s_state.ended_session = s_state.session;
  s_state.session = 0;
  s_state.head = 0;
  s_state.count = 0;
  s_state.generation++;
  s_state.has_frame = false;
  s_state.frame_sequence = 0;
  s_state.frame_error = false;
  portEXIT_CRITICAL(&s_lock);
  frame_reset(false);
  xSemaphoreGive(s_frame_lock);
}

esp_err_t fly_link_get_view(fly_link_view_t *out_view) {
  if (out_view == NULL)
    return ESP_ERR_INVALID_ARG;
  portENTER_CRITICAL(&s_lock);
  uint64_t now = now_ms();
  memset(out_view, 0, sizeof(*out_view));
  out_view->snapshot = s_state.snapshot;
  out_view->generation = s_state.generation;
  out_view->mode = s_state.config[1];
  uint64_t age = s_state.has_snapshot ? now - s_state.received_ms : UINT32_MAX;
  out_view->age_ms = age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
  out_view->queued_events = s_state.count;
  out_view->is_open = s_state.is_open;
  out_view->is_connected = s_state.session != 0;
  out_view->has_snapshot = s_state.has_snapshot;
  out_view->is_stale = !is_fresh(now);
  out_view->has_overflow = s_state.has_overflow;
  uint64_t frame_age = s_state.has_frame ? now - s_state.frame_received_ms : UINT32_MAX;
  out_view->frame_age_ms = frame_age > UINT32_MAX ? UINT32_MAX : (uint32_t)frame_age;
  out_view->frame_sequence = s_state.frame_sequence;
  out_view->has_frame = s_state.has_frame;
  out_view->is_frame_stale = !is_fresh(now) || !s_state.has_frame || frame_age >= FLY_LINK_STALE_MS;
  out_view->frame_error = s_state.frame_error;
  portEXIT_CRITICAL(&s_lock);
  return ESP_OK;
}

esp_err_t
fly_link_frame_copy(uint32_t generation, uint32_t sequence, uint8_t *out, size_t capacity) {
  if (out == NULL || capacity < FLY_LINK_FRAME_BYTES)
    return ESP_ERR_INVALID_ARG;
  if (s_frame_lock == NULL)
    return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(s_frame_lock, portMAX_DELAY);
  portENTER_CRITICAL(&s_lock);
  uint64_t now = now_ms();
  bool valid = generation == s_state.generation && sequence != 0 &&
               sequence == s_state.frame_sequence && s_state.is_open &&
               s_state.config[1] == FLY_LINK_MODE_VISUAL_3D && s_state.has_frame && is_fresh(now) &&
               now - s_state.frame_received_ms < FLY_LINK_STALE_MS;
  portEXIT_CRITICAL(&s_lock);
  if (valid && s_frame_front != NULL)
    memcpy(out, s_frame_front, FLY_LINK_FRAME_BYTES);
  else
    valid = false;
  xSemaphoreGive(s_frame_lock);
  return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t fly_link_enqueue(
    fly_link_action_t action, uint16_t x, uint16_t y, uint16_t value, uint32_t generation) {
  if (action < FLY_LINK_ACTION_SWEET || action >= FLY_LINK_ACTION_COUNT)
    return ESP_ERR_INVALID_ARG;
  esp_err_t result = ESP_OK;
  portENTER_CRITICAL(&s_lock);
  uint64_t now = now_ms();
  if ((action > FLY_LINK_ACTION_RESET && s_state.config[1] != FLY_LINK_MODE_VISUAL_3D) ||
      (action == FLY_LINK_ACTION_SCROLL && value != 1)) {
    result = ESP_ERR_INVALID_ARG;
  } else if (generation != s_state.generation || !s_state.is_open || !is_fresh(now)) {
    result = ESP_ERR_INVALID_STATE;
  } else if (s_state.count == FLY_LINK_QUEUE_SIZE || s_state.generated == UINT32_MAX) {
    s_state.has_overflow = true;
    result = ESP_ERR_NO_MEM;
  } else {
    uint8_t index = (s_state.head + s_state.count) % FLY_LINK_QUEUE_SIZE;
    fly_link_event_t *event = &s_state.events[index];
    event->sequence = ++s_state.generated;
    event->action = (uint8_t)action;
    event->x = x;
    event->y = y;
    event->value = value;
    s_state.count++;
  }
  portEXIT_CRITICAL(&s_lock);
  return result;
}

uint8_t fly_link_handle(uint16_t cmd,
                        const uint8_t *payload,
                        uint16_t plen,
                        uint8_t *out,
                        uint16_t out_cap,
                        uint16_t *out_len) {
  if (out_len == NULL)
    return SPI_STATUS_INVALID_ARG;
  *out_len = 0;
  if (payload == NULL || out == NULL || out_cap < FLY_LINK_RESPONSE_MAX)
    return SPI_STATUS_INVALID_ARG;
  bool is_config = cmd == SPI_ID_FLY_CONFIG;
  bool is_exchange = cmd == SPI_ID_FLY_EXCHANGE;
  bool is_stop = cmd == SPI_ID_FLY_STOP;
  bool is_frame = cmd == FLY_LINK_FRAME_CMD;
  if (!is_config && !is_exchange && !is_stop && !is_frame)
    return SPI_STATUS_UNSUPPORTED;
  if (s_frame_lock == NULL)
    return SPI_STATUS_ERROR;
  if (is_frame) {
    xSemaphoreTake(s_frame_lock, portMAX_DELAY);
    uint8_t result = frame_handle(payload, plen, out, out_len);
    xSemaphoreGive(s_frame_lock);
    return result;
  }
  if ((is_config && plen != FLY_LINK_CONFIG_SIZE) ||
      (is_exchange && plen != FLY_LINK_SNAPSHOT_SIZE) || (is_stop && plen != FLY_LINK_STOP_SIZE) ||
      payload[0] != FLY_LINK_VERSION)
    return SPI_STATUS_INVALID_ARG;
  if (is_config && !config_valid(payload))
    return SPI_STATUS_INVALID_ARG;
  if (is_stop && (payload[1] != 0 || payload[2] != 0 || payload[3] != 0))
    return SPI_STATUS_INVALID_ARG;

  uint8_t result;
  xSemaphoreTake(s_frame_lock, portMAX_DELAY);
  // Allocate only on an open UI's valid mode-1 CONFIG, never at boot. No
  // fallback to scarce internal SRAM when PSRAM is unavailable.
  if (is_config && payload[1] == FLY_LINK_MODE_VISUAL_3D) {
    portENTER_CRITICAL(&s_lock);
    bool open = s_state.is_open;
    portEXIT_CRITICAL(&s_lock);
    if (open && s_frame_front == NULL) {
      s_frame_front = heap_caps_malloc(FLY_LINK_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      s_frame_back = heap_caps_malloc(FLY_LINK_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (s_frame_front == NULL || s_frame_back == NULL) {
        frame_reset(true);
        xSemaphoreGive(s_frame_lock);
        return SPI_STATUS_ERROR;
      }
    }
  }
  portENTER_CRITICAL(&s_lock);
  uint64_t now = now_ms();
  uint32_t old_generation = s_state.generation;
  if (is_stop) {
    uint32_t session = read_u32(payload + FLY_U32_BYTES);
    if (session == 0 || (session != s_state.session &&
                         (s_state.session != 0 || session != s_state.ended_session))) {
      result = SPI_STATUS_INVALID_ARG;
    } else {
      s_state.ended_session = session;
      s_state.session = 0;
      s_state.count = 0;
      s_state.head = 0;
      s_state.has_frame = false;
      s_state.frame_sequence = 0;
      result = SPI_STATUS_OK;
    }
  } else if (!s_state.is_open) {
    result = SPI_STATUS_BUSY;
  } else {
    result = is_config ? configure(payload) : exchange(payload, now);
  }
  if (result == SPI_STATUS_OK)
    encode_response(out, out_len, is_exchange, now);
  bool changed = old_generation != s_state.generation;
  portEXIT_CRITICAL(&s_lock);
  if (result == SPI_STATUS_OK && (changed || is_stop))
    frame_reset(is_config && payload[1] == FLY_LINK_MODE_TEST);
  xSemaphoreGive(s_frame_lock);
  return result;
}

static uint16_t read_u16(const uint8_t *data) {
  return (uint16_t)data[0] | ((uint16_t)data[1] << FLY_BYTE_SHIFT);
}

static uint32_t read_u32(const uint8_t *data) {
  uint32_t value = 0;
  for (uint8_t index = 0; index < FLY_U32_BYTES; index++)
    value |= (uint32_t)data[index] << (index * FLY_BYTE_SHIFT);
  return value;
}

static void write_u16(uint8_t *out, uint16_t value) {
  for (uint8_t index = 0; index < FLY_U16_BYTES; index++)
    out[index] = (uint8_t)(value >> (index * FLY_BYTE_SHIFT));
}

static void write_u32(uint8_t *out, uint32_t value) {
  for (uint8_t index = 0; index < FLY_U32_BYTES; index++)
    out[index] = (uint8_t)(value >> (index * FLY_BYTE_SHIFT));
}

static uint64_t now_ms(void) {
  return (uint64_t)esp_timer_get_time() / FLY_MS_TO_US;
}

static bool is_fresh(uint64_t now) {
  return s_state.session != 0 && s_state.has_snapshot && !s_state.has_overflow &&
         now - s_state.received_ms < FLY_LINK_STALE_MS;
}

static void reset_state(bool is_open) {
  uint32_t generation = s_state.generation + 1;
  uint32_t ended_session = s_state.session != 0 ? s_state.session : s_state.ended_session;
  memset(&s_state, 0, sizeof(s_state));
  s_state.is_open = is_open;
  s_state.generation = generation;
  s_state.ended_session = ended_session;
}

static bool config_valid(const uint8_t *payload) {
  bool mode = payload[1] == FLY_LINK_MODE_TEST || payload[1] == FLY_LINK_MODE_VISUAL_3D;
  if (payload[1] == FLY_LINK_MODE_VISUAL_3D && (read_u16(payload + 8) != FLY_LINK_FRAME_WIDTH ||
                                                read_u16(payload + 10) != FLY_LINK_FRAME_HEIGHT))
    return false;
  return mode && read_u16(payload + 2) == 0 && read_u32(payload + 4) != 0 &&
         read_u16(payload + 8) != 0 && read_u16(payload + 10) != 0 && payload[12] > 0 &&
         payload[12] <= FLY_MAX_RATE_HZ && payload[13] > 0 &&
         payload[13] <= FLY_LINK_STIMULUS_MAX && read_u16(payload + 14) == 0;
}

static uint8_t configure(const uint8_t *payload) {
  uint32_t session = read_u32(payload + 4);
  if (s_state.session == session)
    return memcmp(payload, s_state.config, FLY_LINK_CONFIG_SIZE) == 0 ? SPI_STATUS_OK
                                                                      : SPI_STATUS_INVALID_ARG;
  // Reusing an ended id could make delayed old inputs look like a new session.
  if (s_state.ended_session == session)
    return SPI_STATUS_INVALID_ARG;
  reset_state(true);
  s_state.session = session;
  memcpy(s_state.config, payload, FLY_LINK_CONFIG_SIZE);
  return SPI_STATUS_OK;
}

static bool decode_snapshot(const uint8_t *payload, fly_link_snapshot_t *out_snapshot) {
  if (payload[1] != s_state.config[1] || (payload[2] & ~FLY_LINK_FLAG_PAUSED) != 0 ||
      payload[3] > FLY_LINK_STIMULUS_MAX || read_u16(payload + 24) >= FLY_LINK_HEADING_MAX ||
      payload[26] >= FLY_ANIMATION_COUNT || payload[27] > FLY_LINK_ACTIVITY_MAX ||
      read_u16(payload + 38) != 0 ||
      ((payload[2] & FLY_LINK_FLAG_PAUSED) != 0 && read_u16(payload + 36) != 0))
    return false;
  memset(out_snapshot, 0, sizeof(*out_snapshot));
  out_snapshot->mode = payload[1];
  out_snapshot->is_paused = (payload[2] & FLY_LINK_FLAG_PAUSED) != 0;
  out_snapshot->stimulus_count = payload[3];
  out_snapshot->session = read_u32(payload + 4);
  out_snapshot->sequence = read_u32(payload + FLY_SEQUENCE_OFFSET);
  out_snapshot->sim_time_ms = read_u32(payload + FLY_TIME_OFFSET);
  out_snapshot->x = read_u16(payload + 20);
  out_snapshot->y = read_u16(payload + 22);
  out_snapshot->heading = read_u16(payload + 24);
  out_snapshot->animation = payload[26];
  out_snapshot->activity_count = payload[27];
  out_snapshot->speed_milli = read_u16(payload + 36);
  for (uint8_t index = 0; index < FLY_LINK_ACTIVITY_MAX; index++) {
    uint16_t value = read_u16(payload + FLY_ACTIVITY_OFFSET + index * FLY_U16_BYTES);
    if (value > FLY_LINK_ACTIVITY_SCALE || (index >= payload[27] && value != 0))
      return false;
    out_snapshot->activity[index] = value;
  }
  for (uint8_t index = 0; index < FLY_LINK_STIMULUS_MAX; index++) {
    const uint8_t *entry = payload + FLY_STIMULUS_OFFSET + index * FLY_STIMULUS_BYTES;
    if (index >= payload[3]) {
      for (uint8_t byte = 0; byte < FLY_STIMULUS_BYTES; byte++)
        if (entry[byte] != 0)
          return false;
    } else {
      if ((entry[4] != FLY_STIMULUS_SWEET && entry[4] != FLY_STIMULUS_TOUCH) || entry[5] == 0)
        return false;
      out_snapshot->stimuli[index].x = read_u16(entry);
      out_snapshot->stimuli[index].y = read_u16(entry + 2);
      out_snapshot->stimuli[index].kind = entry[4];
      out_snapshot->stimuli[index].strength = entry[5];
    }
  }
  return true;
}

static uint8_t exchange(const uint8_t *payload, uint64_t now) {
  fly_link_snapshot_t snapshot;
  if (!decode_snapshot(payload, &snapshot) || s_state.session == 0 ||
      snapshot.session != s_state.session || snapshot.stimulus_count > s_state.config[13] ||
      snapshot.sequence == 0 || snapshot.sequence < s_state.sequence)
    return SPI_STATUS_INVALID_ARG;
  if (snapshot.sequence == s_state.sequence)
    return memcmp(payload, s_state.raw, FLY_LINK_SNAPSHOT_SIZE) == 0 ? SPI_STATUS_OK
                                                                     : SPI_STATUS_INVALID_ARG;
  if (s_state.sequence == 0 && snapshot.sequence != 1)
    return SPI_STATUS_INVALID_ARG;
  uint32_t ack = read_u32(payload + FLY_ACK_OFFSET);
  if (ack < s_state.acknowledged || ack > s_state.sent)
    return SPI_STATUS_INVALID_ARG;
  bool can_reset_time = false;
  for (uint8_t index = 0; index < s_state.count; index++) {
    const fly_link_event_t *event = &s_state.events[(s_state.head + index) % FLY_LINK_QUEUE_SIZE];
    if (event->sequence <= ack && event->action == FLY_LINK_ACTION_RESET)
      can_reset_time = true;
  }
  if (s_state.sequence != 0 && snapshot.sim_time_ms < read_u32(s_state.raw + FLY_TIME_OFFSET) &&
      !can_reset_time)
    return SPI_STATUS_INVALID_ARG;
  while (s_state.count != 0 && s_state.events[s_state.head].sequence <= ack) {
    s_state.head = (s_state.head + 1) % FLY_LINK_QUEUE_SIZE;
    s_state.count--;
  }
  s_state.acknowledged = ack;
  s_state.sequence = snapshot.sequence;
  memcpy(s_state.raw, payload, FLY_LINK_SNAPSHOT_SIZE);
  if (!s_state.has_overflow) {
    s_state.snapshot = snapshot;
    s_state.has_snapshot = true;
    s_state.received_ms = now;
  }
  return SPI_STATUS_OK;
}

static void encode_response(uint8_t *out, uint16_t *out_len, bool with_events, uint64_t now) {
  uint8_t count = with_events ? s_state.count : 0;
  if (count > FLY_LINK_EVENT_BATCH)
    count = FLY_LINK_EVENT_BATCH;
  memset(out, 0, FLY_LINK_RESPONSE_MAX);
  out[0] = FLY_LINK_VERSION;
  out[1] = (s_state.is_open ? FLY_FLAG_OPEN : 0) | (is_fresh(now) ? FLY_FLAG_FRESH : 0) |
           (s_state.has_overflow ? FLY_FLAG_OVERFLOW : 0);
  out[2] = count;
  out[3] = s_state.config[1];
  write_u32(out + 4, s_state.session != 0 ? s_state.session : s_state.ended_session);
  write_u32(out + 8, s_state.sequence);
  write_u32(out + 12, s_state.generated);
  for (uint8_t index = 0; index < count; index++) {
    const fly_link_event_t *event = &s_state.events[(s_state.head + index) % FLY_LINK_QUEUE_SIZE];
    uint8_t *entry = out + FLY_LINK_RESPONSE_HEADER_SIZE + index * FLY_LINK_EVENT_SIZE;
    write_u32(entry, event->sequence);
    entry[4] = event->action;
    write_u16(entry + 6, event->x);
    write_u16(entry + 8, event->y);
    write_u16(entry + 10, event->value);
    if (event->sequence > s_state.sent)
      s_state.sent = event->sequence;
  }
  *out_len = FLY_LINK_RESPONSE_HEADER_SIZE + count * FLY_LINK_EVENT_SIZE;
}

static void frame_reset(bool release_storage) {
  s_frame_started = 0;
  s_frame_next = 0;
  s_frame_pending = false;
  if (release_storage) {
    if (s_frame_front != NULL)
      heap_caps_free(s_frame_front);
    if (s_frame_back != NULL)
      heap_caps_free(s_frame_back);
    s_frame_front = NULL;
    s_frame_back = NULL;
  }
}

static uint8_t frame_reject(uint8_t status) {
  s_frame_pending = false;
  portENTER_CRITICAL(&s_lock);
  s_state.frame_error = true;
  portEXIT_CRITICAL(&s_lock);
  return status;
}

static uint8_t
frame_handle(const uint8_t *payload, uint16_t plen, uint8_t *out, uint16_t *out_len) {
  if (plen < FLY_LINK_FRAME_HEADER_SIZE || plen > FLY_LINK_FRAME_PAYLOAD_MAX ||
      payload[0] != FLY_LINK_VERSION || payload[1] > 1 || read_u16(payload + 2) != 0)
    return frame_reject(SPI_STATUS_INVALID_ARG);
  const uint32_t session = read_u32(payload + 4);
  const uint32_t sequence = read_u32(payload + 8);
  const uint32_t offset = read_u32(payload + 12);
  const uint16_t count = read_u16(payload + 16);
  const uint16_t flags = read_u16(payload + 18);
  const bool begin = (flags & 1U) != 0;
  const bool end = (flags & 2U) != 0;
  portENTER_CRITICAL(&s_lock);
  bool allowed = s_state.is_open && s_state.session != 0 &&
                 s_state.config[1] == FLY_LINK_MODE_VISUAL_3D && is_fresh(now_ms());
  bool matching = session != 0 && session == s_state.session;
  portEXIT_CRITICAL(&s_lock);
  if (!allowed || s_frame_front == NULL || s_frame_back == NULL)
    return frame_reject(SPI_STATUS_BUSY);
  if (!matching || sequence == 0 || count == 0 || count > FLY_LINK_FRAME_CHUNK_PIXELS ||
      offset >= FLY_LINK_FRAME_PIXELS || count > FLY_LINK_FRAME_PIXELS - offset ||
      (flags & ~3U) != 0 || end != (offset + count == FLY_LINK_FRAME_PIXELS) ||
      (begin ? (offset != 0 || sequence <= s_frame_started)
             : (!s_frame_pending || sequence != s_frame_started || offset != s_frame_next)))
    return frame_reject(SPI_STATUS_INVALID_ARG);

  const uint8_t *encoded = payload + FLY_LINK_FRAME_HEADER_SIZE;
  const size_t bytes = plen - FLY_LINK_FRAME_HEADER_SIZE;
  if (payload[1] == 0) {
    if (bytes != (size_t)count * 2U)
      return frame_reject(SPI_STATUS_INVALID_ARG);
  } else {
    if (bytes == 0 || bytes % 3U != 0)
      return frame_reject(SPI_STATUS_INVALID_ARG);
    uint32_t decoded = 0;
    for (size_t i = 0; i < bytes; i += 3U) {
      if (encoded[i] == 0 || encoded[i] > count - decoded)
        return frame_reject(SPI_STATUS_INVALID_ARG);
      decoded += encoded[i];
    }
    if (decoded != count)
      return frame_reject(SPI_STATUS_INVALID_ARG);
  }

  // Only validated fragments touch the back buffer; front stays immutable
  // until a complete frame's pointer swap. No decoding/copying under portMUX.
  if (begin) {
    s_frame_started = sequence;
    s_frame_next = 0;
    s_frame_pending = true;
    portENTER_CRITICAL(&s_lock);
    s_state.frame_error = false;
    portEXIT_CRITICAL(&s_lock);
  }
  uint8_t *destination = s_frame_back + (size_t)offset * 2U;
  if (payload[1] == 0)
    memcpy(destination, encoded, bytes);
  else {
    for (size_t i = 0; i < bytes; i += 3U)
      for (uint16_t run = 0; run < encoded[i]; run++) {
        *destination++ = encoded[i + 1];
        *destination++ = encoded[i + 2];
      }
  }
  s_frame_next = offset + count;
  if (end) {
    uint8_t *previous = s_frame_front;
    s_frame_front = s_frame_back;
    s_frame_back = previous;
    s_frame_pending = false;
    portENTER_CRITICAL(&s_lock);
    s_state.frame_sequence = sequence;
    s_state.frame_received_ms = now_ms();
    s_state.has_frame = true;
    s_state.frame_error = false;
    portEXIT_CRITICAL(&s_lock);
  }
  memset(out, 0, FLY_LINK_FRAME_RESPONSE_SIZE);
  out[0] = FLY_LINK_VERSION;
  out[1] = FLY_LINK_MODE_VISUAL_3D;
  write_u32(out + 4, session);
  write_u32(out + 8, sequence);
  write_u32(out + 12, s_frame_next);
  portENTER_CRITICAL(&s_lock);
  write_u32(out + 16, s_state.frame_sequence);
  portEXIT_CRITICAL(&s_lock);
  *out_len = FLY_LINK_FRAME_RESPONSE_SIZE;
  return SPI_STATUS_OK;
}
