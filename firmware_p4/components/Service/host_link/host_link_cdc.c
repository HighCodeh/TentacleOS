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

// USB CDC-ACM transport for the companion host link. The CDC RX callback runs
// in the TinyUSB task, so it only buffers bytes into a stream buffer; a worker
// task drains them into host_link_feed(), where dispatch may block on the SPI
// bridge — never block inside the USB callback.

#include "host_link.h"
#include "host_link_sec.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "sys_prio.h"

#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tusb_desc.h"

static const char *TAG = "HOST_LINK_CDC";

#define HOST_LINK_CDC_ITF        TINYUSB_CDC_ACM_0
#define HOST_LINK_CDC_RX_CHUNK   64
#define HOST_LINK_CDC_STREAM     1024
#define HOST_LINK_CDC_TASK_STK   8192 // dispatch routes into SPI + deep ops; 4K ran dry
#define HOST_LINK_CDC_TASK_PRIO SYS_PRIO_SERVICE_HI
#define HOST_LINK_CDC_FLUSH_MS   50
#define HOST_LINK_CDC_MAX_STALLS 4 // give up a write after this many full-buffer stalls

static StreamBufferHandle_t s_rx_stream = NULL;
static TaskHandle_t s_worker = NULL;

static void cdc_rx_cb(int itf, cdcacm_event_t *event) {
  (void)event;
  uint8_t buf[HOST_LINK_CDC_RX_CHUNK];
  size_t rx = 0;
  if (tinyusb_cdcacm_read((tinyusb_cdcacm_itf_t)itf, buf, sizeof(buf), &rx) == ESP_OK && rx > 0) {
    // Non-blocking: if the worker is behind, drop rather than stall the USB task.
    xStreamBufferSend(s_rx_stream, buf, rx, 0);
  }
}

static void cdc_write(const uint8_t *frame, size_t len);

static void cdc_line_state_cb(int itf, cdcacm_event_t *event) {
  (void)itf;
  // Claim the single companion session when the host opens the port (DTR set),
  // release it when the port closes. Release also resets crypto + reassembly so
  // a reconnecting app must re-handshake.
  if (event->line_state_changed_data.dtr) {
    host_link_session_acquire(cdc_write);
  } else {
    host_link_session_release(cdc_write);
  }
}

static void cdc_write(const uint8_t *frame, size_t len) {
  // Drop when no app has the port open. Logs are pushed regardless of a
  // connection, so without this guard the write loop would spin forever.
  if (!tud_cdc_n_connected(HOST_LINK_CDC_ITF))
    return;

  size_t off = 0;
  int stalls = 0;
  while (off < len) {
    size_t q = tinyusb_cdcacm_write_queue(HOST_LINK_CDC_ITF, frame + off, len - off);
    off += q;
    if (q == 0) {
      // TX buffer full — flush to make room. Give up after a few stalls so a
      // wedged endpoint can't block the caller indefinitely.
      if (++stalls > HOST_LINK_CDC_MAX_STALLS)
        return;
      tinyusb_cdcacm_write_flush(HOST_LINK_CDC_ITF, pdMS_TO_TICKS(HOST_LINK_CDC_FLUSH_MS));
    } else {
      stalls = 0;
    }
  }
  tinyusb_cdcacm_write_flush(HOST_LINK_CDC_ITF, pdMS_TO_TICKS(HOST_LINK_CDC_FLUSH_MS));
}

static void host_link_worker(void *arg) {
  (void)arg;
  uint8_t buf[128];
  for (;;) {
    size_t n = xStreamBufferReceive(s_rx_stream, buf, sizeof(buf), portMAX_DELAY);
    // Only feed if USB owns the session; otherwise (BLE active) drop the bytes.
    if (n > 0 && host_link_session_owns(cdc_write)) {
      host_link_feed(buf, n);
    }
  }
}

esp_err_t host_link_cdc_init(void) {
  static bool s_cdc_up = false;
  if (s_cdc_up) {
    return ESP_OK; // already brought up (e.g. native-USB toggle re-invoked it)
  }

  // Defer the native P4 USB (TinyUSB) until the USB-C data mux is actually routed
  // to it. At boot the mux is on the UART bridge (CP2105 serial console): bringing
  // up the native PHY there enumerates a second device on the shared connector and
  // fights the serial port (host has to replug). Install on demand instead, when
  // the user switches to native USB.
  if (!usb_mux_is_native()) {
    ESP_LOGI(TAG, "CDC deferred: mux on UART bridge, native USB off");
    return ESP_OK;
  }

  s_rx_stream = xStreamBufferCreate(HOST_LINK_CDC_STREAM, 1);
  if (s_rx_stream == NULL) {
    ESP_LOGE(TAG, "Failed to create RX stream buffer");
    return ESP_ERR_NO_MEM;
  }

  esp_err_t err = busb_init(); // ensure the TinyUSB composite (HID + CDC) is up
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "TinyUSB init failed: %s", esp_err_to_name(err));
    return err;
  }

  const tinyusb_config_cdcacm_t acm_cfg = {
      .cdc_port = HOST_LINK_CDC_ITF,
      .callback_rx = &cdc_rx_cb,
      .callback_line_state_changed = &cdc_line_state_cb,
  };
  err = tinyusb_cdcacm_init(&acm_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "CDC ACM init failed: %s", esp_err_to_name(err));
    return err;
  }

  // The session is claimed on DTR (port open) in cdc_line_state_cb, not here.

  if (xTaskCreatePinnedToCore(host_link_worker, "host_link", HOST_LINK_CDC_TASK_STK, NULL,
                              HOST_LINK_CDC_TASK_PRIO, &s_worker, SYS_CORE_RADIO) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create host_link worker task");
    return ESP_FAIL;
  }

  s_cdc_up = true;
  ESP_LOGI(TAG, "Host link CDC transport up");
  return ESP_OK;
}
