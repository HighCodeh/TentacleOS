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
#include <sys/time.h>

#include "esp_log.h"

#include "pcap_serializer.h"
#include "spi_bridge.h"
#include "spi_session.h"
#include "storage_mkdir.h"
#include "storage_stream.h"
#include "tos_loot.h"
#include "tos_storage_paths.h"

static const char *TAG = "WIFI_SNIFFER";

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
static wifi_sniffer_cb_t s_stream_cb = NULL;
static storage_stream_t s_capture_stream = NULL;
static uint32_t s_session_id = SPI_SESSION_INVALID_ID;

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

static void emit_frame(const uint8_t *data, uint16_t len, int8_t rssi, uint8_t channel) {
  if (s_capture_stream != NULL) {
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
    storage_stream_write(s_capture_stream, &rec, sizeof(rec));
    storage_stream_write(s_capture_stream, &radiotap, sizeof(radiotap));
    storage_stream_write(s_capture_stream, data, len);
  }

  if (s_stream_cb != NULL) {
    s_stream_cb(data, len, rssi, channel);
  }
}

static void session_stream_cb(const uint8_t *payload, uint8_t len) {
  if (payload == NULL || len < sizeof(spi_wifi_sniffer_frame_t))
    return;
  const spi_wifi_sniffer_frame_t *frag = (const spi_wifi_sniffer_frame_t *)payload;

  uint16_t avail = (uint16_t)(len - sizeof(spi_wifi_sniffer_frame_t));
  uint16_t frag_len = frag->frag_len;
  if (frag_len > avail)
    frag_len = avail; // never read past what actually arrived

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

static void update_stats(void) {
  spi_header_t resp;
  uint16_t magic_stats = SPI_DATA_INDEX_STATS;
  spi_bridge_send_command(SPI_ID_SYSTEM_DATA,
                          (uint8_t *)&magic_stats,
                          2,
                          &resp,
                          (uint8_t *)&s_cached_stats,
                          sizeof(s_cached_stats),
                          1000);
}

static bool
start_internal(wifi_sniffer_type_t type, uint8_t channel, bool monitor_mode, wifi_sniffer_cb_t cb) {
  uint8_t payload[3];
  payload[0] = (uint8_t)type;
  payload[1] = channel;
  payload[2] = monitor_mode ? 1 : 0;
  memset(&s_cached_stats, 0, sizeof(s_cached_stats));
  s_stream_cb = cb;
  reasm_reset();

  // Ask the C5 for the whole frame; fragments are reassembled on our side.
  wifi_sniffer_set_snaplen(SPI_WIFI_SNIFFER_FRAME_MAX);

  // Persist to the SD card by default, whoever starts the sniffer (UI, console,
  // ...). A caller that already opened an explicit file keeps its own path.
  if (s_capture_stream == NULL) {
    char path[WIFI_SNIFFER_PATH_MAX];
    auto_capture_path(type, monitor_mode, path, sizeof(path));
    wifi_sniffer_start_capture(path);
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
  return start_internal(type, channel, false, NULL);
}

bool wifi_sniffer_start_stream(wifi_sniffer_type_t type, uint8_t channel, wifi_sniffer_cb_t cb) {
  return start_internal(type, channel, false, cb);
}

bool wifi_sniffer_start_monitor(uint8_t channel) {
  return start_internal(WIFI_SNIFFER_TYPE_RAW, channel, true, NULL);
}

void wifi_sniffer_stop(void) {
  if (s_session_id != SPI_SESSION_INVALID_ID) {
    spi_session_stop(s_session_id);
    s_session_id = SPI_SESSION_INVALID_ID;
  }
  wifi_sniffer_stop_capture();
  reasm_reset();
  s_stream_cb = NULL;
}

uint32_t wifi_sniffer_get_packet_count(void) {
  update_stats();
  return s_cached_stats.packets;
}

uint32_t wifi_sniffer_get_deauth_count(void) {
  return s_cached_stats.deauths;
}

uint32_t wifi_sniffer_get_buffer_usage(void) {
  return s_cached_stats.buffer_usage;
}

bool wifi_sniffer_handshake_captured(void) {
  return s_cached_stats.handshake_captured;
}

bool wifi_sniffer_pmkid_captured(void) {
  return s_cached_stats.pmkid_captured;
}

bool wifi_sniffer_start_capture(const char *path) {
  if (path == NULL)
    return false;
  if (s_capture_stream != NULL) {
    storage_stream_close(s_capture_stream);
    s_capture_stream = NULL;
  }
  ensure_parent_dir(path);

  s_capture_stream = storage_stream_open(path, "wb");
  if (s_capture_stream == NULL) {
    ESP_LOGE(TAG, "Failed to open capture file: %s", path);
    return false;
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
  storage_stream_write(s_capture_stream, &header, sizeof(header));

  ESP_LOGI(TAG, "Capture started: %s", path);
  return true;
}

bool wifi_sniffer_stop_capture(void) {
  if (s_capture_stream == NULL)
    return false;
  storage_stream_flush(s_capture_stream);
  storage_stream_close(s_capture_stream);
  s_capture_stream = NULL;
  ESP_LOGI(TAG, "Capture stopped");
  return true;
}

size_t wifi_sniffer_get_capture_size(void) {
  return storage_stream_bytes_written(s_capture_stream);
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
