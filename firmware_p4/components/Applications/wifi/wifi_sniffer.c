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

#include "wifi_sniffer.h"

#include "led_control.h"

#include <string.h>
#include <stdatomic.h>
#include <sys/time.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "pcap_serializer.h"
#include "wifi_ap_capture.h"
#include "wifi_capture_queue.h"
#include "spi_bridge.h"
#include "spi_session.h"
#include "storage_mkdir.h"
#include "storage_stream.h"
#include "tos_loot.h"
#include "tos_storage_paths.h"

static const char *TAG = "WIFI_SNIFFER";

_Static_assert(WIFI_SPECTRUM_BIN_COUNT == SPI_WIFI_SPECTRUM_BIN_COUNT,
               "P4/C5 spectrum bin count mismatch");
_Static_assert(sizeof(spi_sniffer_stats_t) <= SPI_MAX_PAYLOAD,
               "sniffer statistics exceed one SPI payload");

#define WIFI_SNIFFER_PATH_MAX     256
#define WIFI_SNIFFER_PCAP_SNAPLEN 65535

#define RADIOTAP_PRESENT_FLAGS   (1u << 1)
#define RADIOTAP_PRESENT_CHANNEL (1u << 3)
#define RADIOTAP_PRESENT_DBM     (1u << 5)
#define RADIOTAP_F_FCS           0x10   // frame data ends with a 4-byte FCS
#define RADIOTAP_CHAN_2GHZ       0x0080 // channel flags: 2 GHz band
#define RADIOTAP_CHAN_5GHZ       0x0100 // channel flags: 5 GHz band

// Little-endian on-wire layout; pad_channel keeps chan_freq 2-byte aligned.
typedef struct {
  uint8_t version;
  uint8_t pad;
  uint16_t len;
  uint32_t present;
  uint8_t flags;
  uint8_t pad_channel;
  uint16_t chan_freq;
  uint16_t chan_flags;
  int8_t dbm_signal;
} __attribute__((packed)) radiotap_header_t;

static uint16_t channel_to_freq(uint8_t channel) {
  if (channel >= 1 && channel <= 13)
    return 2407 + channel * 5;
  if (channel == 14)
    return 2484;
  if (channel >= 32)
    return 5000 + channel * 5;
  return 0;
}

static spi_sniffer_stats_t s_cached_stats;
static _Atomic(wifi_sniffer_cb_t) s_stream_cb = NULL;
static storage_stream_t s_capture_stream = NULL;
static StaticSemaphore_t s_capture_mutex_storage;
static SemaphoreHandle_t s_capture_mutex;
static esp_err_t s_capture_error = ESP_OK;
static atomic_bool s_is_ap_capture;
static char s_capture_path[WIFI_SNIFFER_PATH_MAX];
static atomic_uint s_session_id = SPI_SESSION_INVALID_ID;
static uint32_t s_reasm_session_id = SPI_SESSION_INVALID_ID;

static uint8_t s_reasm_buf[SPI_WIFI_SNIFFER_FRAME_MAX];
static uint16_t s_reasm_total;
static uint16_t s_reasm_have;
static int8_t s_reasm_rssi;
static uint8_t s_reasm_channel;
static bool s_reasm_active;

bool wifi_sniffer_stop_capture(void);

static void ensure_parent_dir(const char *path) {
  char dir[WIFI_SNIFFER_PATH_MAX];
  strncpy(dir, path, sizeof(dir) - 1);
  dir[sizeof(dir) - 1] = '\0';
  char *slash = strrchr(dir, '/');
  if (slash != NULL && slash != dir) {
    *slash = '\0';
    storage_mkdir_recursive(dir);
  }
}

static void
auto_capture_path(wifi_sniffer_type_t type, bool monitor_mode, char *out, size_t out_size) {
  const char *dir = TOS_PATH_WIFI_LOOT_PCAPS;
  const char *prefix = "raw";
  if (monitor_mode || type == WIFI_SNIFFER_TYPE_EAPOL) {
    dir = TOS_PATH_WIFI_LOOT_HS;
    prefix = "handshake";
  } else if (type == WIFI_SNIFFER_TYPE_BEACON) {
    prefix = "beacon";
  } else if (type == WIFI_SNIFFER_TYPE_PROBE) {
    prefix = "probe";
  } else if (type == WIFI_SNIFFER_TYPE_PMKID) {
    prefix = "pmkid";
  }
  tos_loot_generate_path(dir, prefix, "pcap", out, out_size, NULL, 0);
}

static void reasm_reset(void) {
  s_reasm_active = false;
  s_reasm_total = 0;
  s_reasm_have = 0;
}

static esp_err_t write_ap_frame(const wifi_capture_queue_frame_t *frame) {
  xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
  if (s_capture_error == ESP_OK)
    s_capture_error = wifi_ap_capture_frame_at(frame->data, frame->len, &frame->received_at);
  esp_err_t result = s_capture_error;
  xSemaphoreGive(s_capture_mutex);
  return result;
}

static void emit_frame(const uint8_t *data, uint16_t len, int8_t rssi, uint8_t channel) {
  if (spi_session_callback_id() != atomic_load(&s_session_id))
    return;
  if (atomic_load(&s_is_ap_capture)) {
    wifi_capture_queue_push(data, len);
    wifi_sniffer_cb_t cb = atomic_load(&s_stream_cb);
    if (cb != NULL)
      cb(data, len, rssi, channel);
    return;
  }
  if (s_capture_mutex == NULL)
    return;
  xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
  if (spi_session_callback_id() != atomic_load(&s_session_id)) {
    xSemaphoreGive(s_capture_mutex);
    return;
  }
  if (s_capture_stream != NULL && s_capture_error == ESP_OK) {
    const radiotap_header_t radiotap = {
        .version = 0,
        .pad = 0,
        .len = sizeof(radiotap_header_t),
        .present = RADIOTAP_PRESENT_FLAGS | RADIOTAP_PRESENT_CHANNEL | RADIOTAP_PRESENT_DBM,
        .flags = RADIOTAP_F_FCS,
        .pad_channel = 0,
        .chan_freq = channel_to_freq(channel),
        .chan_flags = (channel >= 32) ? RADIOTAP_CHAN_5GHZ : RADIOTAP_CHAN_2GHZ,
        .dbm_signal = rssi,
    };

    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint32_t total_len = sizeof(radiotap) + len;
    const pcap_packet_header_t rec = {
        .ts_sec = (uint32_t)tv.tv_sec,
        .ts_usec = (uint32_t)tv.tv_usec,
        .incl_len = total_len,
        .orig_len = total_len,
    };
    s_capture_error = storage_stream_write(s_capture_stream, &rec, sizeof(rec));
    if (s_capture_error == ESP_OK)
      s_capture_error = storage_stream_write(s_capture_stream, &radiotap, sizeof(radiotap));
    if (s_capture_error == ESP_OK)
      s_capture_error = storage_stream_write(s_capture_stream, data, len);
    if (s_capture_error != ESP_OK)
      ESP_LOGE(TAG, "PCAP write failed; capture is incomplete");
  }

  xSemaphoreGive(s_capture_mutex);

  wifi_sniffer_cb_t cb = atomic_load(&s_stream_cb);
  if (cb != NULL)
    cb(data, len, rssi, channel);
}

static void session_stream_cb(const uint8_t *payload, uint8_t len) {
  uint32_t session_id = spi_session_callback_id();
  if (session_id == SPI_SESSION_INVALID_ID || session_id != atomic_load(&s_session_id))
    return;
  if (s_reasm_session_id != session_id) {
    reasm_reset();
    s_reasm_session_id = session_id;
  }
  if (payload == NULL || len < sizeof(spi_wifi_sniffer_frame_t))
    return;
  const spi_wifi_sniffer_frame_t *frag = (const spi_wifi_sniffer_frame_t *)payload;

  uint16_t avail = (uint16_t)(len - sizeof(spi_wifi_sniffer_frame_t));
  uint16_t frag_len = frag->frag_len;
  if (frag_len == 0 || frag_len > avail) {
    reasm_reset();
    return;
  }

  if (frag->frag_off == 0) {
    // Start of a new frame; abandon any partial one still in the buffer.
    reasm_reset();
    if (frag->total_len == 0 || frag->total_len > sizeof(s_reasm_buf))
      return; // implausible or larger than we can hold
    s_reasm_total = frag->total_len;
    s_reasm_rssi = frag->rssi;
    s_reasm_channel = frag->channel;
    s_reasm_active = true;
  } else if (!s_reasm_active || frag->total_len != s_reasm_total ||
             frag->frag_off != s_reasm_have) {
    reasm_reset(); // gap, reorder, or mismatched frame: unrecoverable
    return;
  }

  if ((uint32_t)s_reasm_have + frag_len > s_reasm_total) {
    reasm_reset(); // more data than the frame declared
    return;
  }
  memcpy(s_reasm_buf + s_reasm_have, frag->data, frag_len);
  s_reasm_have = (uint16_t)(s_reasm_have + frag_len);

  if (frag->flags & SPI_WIFI_SNIFFER_FRAG_MORE)
    return; // wait for the rest

  if (s_reasm_have == s_reasm_total)
    emit_frame(s_reasm_buf, s_reasm_have, s_reasm_rssi, s_reasm_channel);
  reasm_reset();
}

static void session_lost_cb(uint32_t session_id, spi_id_t op_id) {
  (void)op_id;
  if (session_id == s_session_id) {
    s_session_id = SPI_SESSION_INVALID_ID;
    wifi_sniffer_stop_capture();
    s_stream_cb = NULL;
  }
}

static bool update_stats(void) {
  spi_header_t resp;
  uint16_t magic_stats = SPI_DATA_INDEX_STATS;
  return spi_bridge_send_command(SPI_ID_SYSTEM_DATA,
                                 (uint8_t *)&magic_stats,
                                 2,
                                 &resp,
                                 (uint8_t *)&s_cached_stats,
                                 sizeof(s_cached_stats),
                                 1000) == ESP_OK;
}

static bool start_internal(wifi_sniffer_type_t type,
                           uint8_t channel,
                           bool monitor_mode,
                           wifi_analysis_band_t band,
                           wifi_sniffer_cb_t cb) {
  if (band > WIFI_ANALYSIS_BAND_5G || type >= WIFI_SNIFFER_TYPE_COUNT ||
      s_session_id != SPI_SESSION_INVALID_ID || s_is_ap_capture)
    return false;
  uint8_t payload[4];
  payload[0] = (uint8_t)type;
  payload[1] = channel;
  payload[2] = monitor_mode ? 1 : 0;
  payload[3] = (uint8_t)band;
  memset(&s_cached_stats, 0, sizeof(s_cached_stats));
  s_stream_cb = cb;

  // Ask the C5 for the whole frame; fragments are reassembled on our side.
  wifi_sniffer_set_snaplen(SPI_WIFI_SNIFFER_FRAME_MAX);

  // Persist to the SD card by default, whoever starts the sniffer (UI, console,
  // ...). A caller that already opened an explicit file keeps its own path.
  if (monitor_mode) {
    s_capture_path[0] = '\0';
  } else if (s_capture_stream == NULL &&
             (type == WIFI_SNIFFER_TYPE_EAPOL || type == WIFI_SNIFFER_TYPE_PMKID)) {
    if (s_capture_mutex == NULL)
      s_capture_mutex = xSemaphoreCreateMutexStatic(&s_capture_mutex_storage);
    xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
    s_capture_error = wifi_ap_capture_start();
    s_is_ap_capture = s_capture_error == ESP_OK;
    if (s_is_ap_capture) {
      s_capture_error = wifi_capture_queue_start(write_ap_frame);
      if (s_capture_error != ESP_OK) {
        wifi_ap_capture_stop();
        s_is_ap_capture = false;
      }
    }
    if (s_is_ap_capture) {
      strncpy(s_capture_path, TOS_PATH_WIFI_LOOT_HS, sizeof(s_capture_path) - 1);
      s_capture_path[sizeof(s_capture_path) - 1] = '\0';
    }
    xSemaphoreGive(s_capture_mutex);
    if (!s_is_ap_capture) {
      s_stream_cb = NULL;
      return false;
    }
  } else if (s_capture_stream == NULL) {
    char path[WIFI_SNIFFER_PATH_MAX];
    auto_capture_path(type, monitor_mode, path, sizeof(path));
    if (!wifi_sniffer_start_capture(path)) {
      s_stream_cb = NULL;
      return false;
    }
  }

  s_session_id = spi_session_start(
      SPI_ID_WIFI_APP_SNIFFER, payload, sizeof(payload), session_stream_cb, session_lost_cb);
  if (s_session_id == SPI_SESSION_INVALID_ID) {
    wifi_sniffer_stop_capture();
    s_stream_cb = NULL;
    led_signal_error();
    return false;
  }
  led_signal_info(); // sniffer session up
  return true;
}

bool wifi_sniffer_start(wifi_sniffer_type_t type, uint8_t channel) {
  return start_internal(type, channel, false, WIFI_ANALYSIS_BAND_DUAL, NULL);
}

bool wifi_sniffer_start_band(wifi_sniffer_type_t type, uint8_t channel, wifi_analysis_band_t band) {
  return start_internal(type, channel, false, band, NULL);
}

bool wifi_sniffer_start_stream(wifi_sniffer_type_t type, uint8_t channel, wifi_sniffer_cb_t cb) {
  return start_internal(type, channel, false, WIFI_ANALYSIS_BAND_DUAL, cb);
}

bool wifi_sniffer_start_monitor(uint8_t channel) {
  return start_internal(WIFI_SNIFFER_TYPE_RAW, channel, true, WIFI_ANALYSIS_BAND_DUAL, NULL);
}

bool wifi_sniffer_start_monitor_band(uint8_t channel, wifi_analysis_band_t band) {
  return start_internal(WIFI_SNIFFER_TYPE_RAW, channel, true, band, NULL);
}

void wifi_sniffer_stop(void) {
  uint32_t session_id = atomic_exchange(&s_session_id, SPI_SESSION_INVALID_ID);
  if (session_id != SPI_SESSION_INVALID_ID)
    spi_session_stop(session_id);
  wifi_sniffer_stop_capture();
  s_stream_cb = NULL;
}

uint32_t wifi_sniffer_get_packet_count(void) {
  update_stats();
  return s_cached_stats.packets;
}

bool wifi_sniffer_get_stats(wifi_sniffer_stats_t *out) {
  if (out == NULL || !update_stats())
    return false;
  *out = (wifi_sniffer_stats_t){
      .packets = s_cached_stats.packets,
      .deauths = s_cached_stats.deauths,
      .buffer_usage = s_cached_stats.buffer_usage,
      .beacons = s_cached_stats.beacons,
      .probe_reqs = s_cached_stats.probe_reqs,
      .probe_resps = s_cached_stats.probe_resps,
      .data_frames = s_cached_stats.data_frames,
      .ctrl_frames = s_cached_stats.ctrl_frames,
      .mgmt_frames = s_cached_stats.mgmt_frames,
      .pkts_2ghz = s_cached_stats.pkts_2ghz,
      .pkts_5ghz = s_cached_stats.pkts_5ghz,
      .unique_aps = s_cached_stats.unique_aps,
      .channel = s_cached_stats.channel,
      .last_rssi = s_cached_stats.last_rssi,
      .handshake_captured = s_cached_stats.handshake_captured,
      .pmkid_captured = s_cached_stats.pmkid_captured,
      .capture_attempts = s_cached_stats.capture_attempts,
      .capture_failures = s_cached_stats.capture_failures,
      .capture_hold_seconds = s_cached_stats.capture_hold_seconds,
      .capture_ap_attempt = s_cached_stats.capture_ap_attempt,
  };
  memcpy(out->channel_packets, s_cached_stats.channel_packets, sizeof(out->channel_packets));
  memcpy(out->channel_peak_rssi, s_cached_stats.channel_peak_rssi, sizeof(out->channel_peak_rssi));
  return true;
}

uint32_t wifi_sniffer_get_deauth_count(void) {
  return s_cached_stats.deauths;
}

uint32_t wifi_sniffer_get_buffer_usage(void) {
  return s_cached_stats.buffer_usage;
}

// Recognition runs on C5 using handshake logic adapted from Gutem and contributors.
// https://github.com/Gutem/pwnagotchi-esp32
// P4 exposes the result received over SPI; the upstream copyright and GPL-3.0-only
// notice are beside C5 track_handshake. See docs/wifi/README.md for attribution.
bool wifi_sniffer_handshake_captured(void) {
  return s_cached_stats.handshake_captured;
}

bool wifi_sniffer_pmkid_captured(void) {
  return s_cached_stats.pmkid_captured;
}

bool wifi_sniffer_start_capture(const char *path) {
  bool is_ok = false;
  if (path == NULL)
    return false;
  if (s_capture_mutex == NULL)
    s_capture_mutex = xSemaphoreCreateMutexStatic(&s_capture_mutex_storage);
  xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
  if (s_capture_stream != NULL || s_is_ap_capture) {
    xSemaphoreGive(s_capture_mutex);
    return false;
  }
  s_capture_error = ESP_OK;
  s_capture_path[0] = '\0';
  ensure_parent_dir(path);
  s_capture_stream = storage_stream_open(path, "wb");
  if (s_capture_stream == NULL) {
    ESP_LOGE(TAG, "Failed to open capture file: %s", path);
    s_capture_error = ESP_FAIL;
    goto cleanup;
  }
  const pcap_global_header_t header = {
      .magic_number = PCAP_MAGIC_NUMBER,
      .version_major = PCAP_VERSION_MAJOR,
      .version_minor = PCAP_VERSION_MINOR,
      .thiszone = 0,
      .sigfigs = 0,
      .snaplen = WIFI_SNIFFER_PCAP_SNAPLEN,
      .network = PCAP_LINK_TYPE_802_11_RADIOTAP,
  };
  s_capture_error = storage_stream_write(s_capture_stream, &header, sizeof(header));
  if (s_capture_error == ESP_OK)
    s_capture_error = storage_stream_flush(s_capture_stream);
  if (s_capture_error != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize PCAP: %s", path);
    storage_stream_close(s_capture_stream);
    s_capture_stream = NULL;
    goto cleanup;
  }
  strncpy(s_capture_path, path, sizeof(s_capture_path) - 1);
  s_capture_path[sizeof(s_capture_path) - 1] = '\0';
  ESP_LOGI(TAG, "Capture started: %s", path);
cleanup:
  is_ok = s_capture_error == ESP_OK;
  xSemaphoreGive(s_capture_mutex);
  return is_ok;
}

esp_err_t wifi_sniffer_flush_capture(void) {
  if (s_capture_mutex == NULL)
    return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
  esp_err_t result = s_capture_error;
  if (result == ESP_OK && !s_is_ap_capture) {
    if (s_capture_stream == NULL)
      result = ESP_ERR_INVALID_STATE;
    else
      result = s_capture_error = storage_stream_flush(s_capture_stream);
  }
  xSemaphoreGive(s_capture_mutex);
  return result;
}

const char *wifi_sniffer_get_capture_path(void) {
  return s_capture_path;
}

bool wifi_sniffer_stop_capture(void) {
  if (atomic_load(&s_is_ap_capture))
    wifi_capture_queue_stop();
  if (s_capture_mutex == NULL)
    return false;
  xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
  bool is_ok = false;
  if (s_is_ap_capture) {
    wifi_ap_capture_stop();
    s_is_ap_capture = false;
    is_ok = s_capture_error == ESP_OK;
    if (is_ok)
      ESP_LOGI(TAG, "Per-AP capture stopped");
    else
      ESP_LOGE(TAG, "Per-AP capture stopped with error: %s", esp_err_to_name(s_capture_error));
  } else if (s_capture_stream != NULL) {
    esp_err_t flush_error = storage_stream_flush(s_capture_stream);
    esp_err_t close_error = storage_stream_close(s_capture_stream);
    s_capture_stream = NULL;
    if (s_capture_error == ESP_OK)
      s_capture_error = flush_error != ESP_OK ? flush_error : close_error;
    is_ok = s_capture_error == ESP_OK;
    if (!is_ok)
      ESP_LOGE(TAG, "Capture closed with error: %s", esp_err_to_name(s_capture_error));
    else
      ESP_LOGI(TAG, "Capture stopped");
  }
  xSemaphoreGive(s_capture_mutex);
  return is_ok;
}

size_t wifi_sniffer_get_capture_size(void) {
  if (s_capture_mutex == NULL)
    return 0;
  xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
  size_t size =
      s_is_ap_capture ? wifi_ap_capture_bytes() : storage_stream_bytes_written(s_capture_stream);
  xSemaphoreGive(s_capture_mutex);
  return size;
}

void wifi_sniffer_free_buffer(void) {
  spi_bridge_send_command(SPI_ID_WIFI_SNIFFER_FREE_BUFFER, NULL, 0, NULL, NULL, 0, 2000);
  s_cached_stats.buffer_usage = 0;
}

void wifi_sniffer_set_snaplen(uint16_t len) {
  uint8_t payload[2];
  memcpy(payload, &len, sizeof(len));
  spi_bridge_send_command(
      SPI_ID_WIFI_SNIFFER_SET_SNAPLEN, payload, sizeof(payload), NULL, NULL, 0, 2000);
}

void wifi_sniffer_set_verbose(bool is_verbose) {
  uint8_t payload = is_verbose ? 1 : 0;
  spi_bridge_send_command(SPI_ID_WIFI_SNIFFER_SET_VERBOSE, &payload, 1, NULL, NULL, 0, 2000);
}

void wifi_sniffer_clear_pmkid(void) {
  spi_bridge_send_command(SPI_ID_WIFI_SNIFFER_CLEAR_PMKID, NULL, 0, NULL, NULL, 0, 2000);
  s_cached_stats.pmkid_captured = false;
}

void wifi_sniffer_get_pmkid_bssid(uint8_t out_bssid[6]) {
  if (out_bssid == NULL)
    return;
  spi_header_t resp;
  uint8_t payload[6] = {0};
  if (spi_bridge_send_command(
          SPI_ID_WIFI_SNIFFER_GET_PMKID_BSSID, NULL, 0, &resp, payload, sizeof(payload), 1000) ==
      ESP_OK) {
    memcpy(out_bssid, payload, 6);
  } else {
    memset(out_bssid, 0, 6);
  }
}

void wifi_sniffer_clear_handshake(void) {
  spi_bridge_send_command(SPI_ID_WIFI_SNIFFER_CLEAR_HANDSHAKE, NULL, 0, NULL, NULL, 0, 2000);
  s_cached_stats.handshake_captured = false;
}

void wifi_sniffer_get_handshake_bssid(uint8_t out_bssid[6]) {
  if (out_bssid == NULL)
    return;
  spi_header_t resp;
  uint8_t payload[6] = {0};
  if (spi_bridge_send_command(SPI_ID_WIFI_SNIFFER_GET_HANDSHAKE_BSSID,
                              NULL,
                              0,
                              &resp,
                              payload,
                              sizeof(payload),
                              1000) == ESP_OK) {
    memcpy(out_bssid, payload, 6);
  } else {
    memset(out_bssid, 0, 6);
  }
}

esp_err_t wifi_sniffer_capture_snapshot(wifi_ap_capture_snapshot_t *out_snapshot) {
  if (out_snapshot == NULL)
    return ESP_ERR_INVALID_ARG;
  if (s_capture_mutex == NULL)
    return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(s_capture_mutex, portMAX_DELAY);
  wifi_ap_capture_snapshot(out_snapshot);
  wifi_capture_queue_stats_t queue;
  wifi_capture_queue_get_stats(&queue);
  out_snapshot->pending = queue.pending;
  out_snapshot->queue_peak = queue.peak;
  out_snapshot->dropped = queue.dropped;
  out_snapshot->discarded = queue.discarded;
  esp_err_t result = s_capture_error;
  xSemaphoreGive(s_capture_mutex);
  return result;
}
