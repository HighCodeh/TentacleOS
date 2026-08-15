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

#include "spi_bridge.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_chip_info.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "sys_prio.h"
#include "soc/lp_aon_reg.h"
#include "soc/soc.h"

#include "bt_dispatcher.h"
#include "bluetooth_service.h"
#include "deauther_detector.h"
#include "ota_service.h"
#include "session_manager.h"
#include "signal_monitor.h"
#include "spi_slave_driver.h"
#include "wifi_dispatcher.h"
#include "wifi_service.h"
#include "wifi_sniffer.h"

static const char *TAG = "SPI_BRIDGE_C5";

#define SPI_STREAM_QUEUE_LEN  64
#define SPI_BRIDGE_TASK_STACK 4096
#define SPI_BRIDGE_TASK_PRIO SYS_PRIO_REALTIME
#define SPI_IRQ_PULSE_US      10
#define SPI_RESTART_DELAY_MS  50
#define SPI_FW_VERSION_LEN    32
#define SPI_FW_VERSION_STRING "1.3.0"

typedef struct {
  spi_id_t id;
  uint8_t len;
  uint8_t data[SPI_MAX_PAYLOAD];
} spi_stream_item_t;

static void *s_data_source = NULL;
static uint16_t s_item_count = 0;
static const uint16_t *s_item_count_ptr = NULL;
static uint8_t s_item_size = 0;

static spi_stream_item_t s_stream_queue[SPI_STREAM_QUEUE_LEN];
static uint8_t s_stream_head = 0;
static uint8_t s_stream_tail = 0;
static uint8_t s_stream_count = 0;
static bool s_is_wifi_sniffer_streaming = false;
static bool s_is_bt_sniffer_streaming = false;
static bool s_is_mesh_toradio_streaming = false;
static bool s_is_mcore_rx_streaming = false;
static bool s_is_host_rx_streaming = false;
static bool s_is_system_log_streaming = false;
static bool s_use_irq = true; // false = POLL mode (no IRQ trace); master polls
static portMUX_TYPE s_stream_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_is_restart_pending = false;
static volatile bool s_is_download_pending = false;
static char s_firmware_version[SPI_FW_VERSION_LEN] = "unknown";

static void load_firmware_version(void);
static uint16_t stream_pop_into(uint8_t *buf, uint16_t offset, uint16_t cap);
static void bridge_task(void *pvParameters);

// Public functions

void spi_bridge_provide_results(void *source, uint16_t count, uint8_t item_size) {
  s_data_source = source;
  s_item_count = count;
  s_item_count_ptr = NULL;
  s_item_size = item_size;
}

void spi_bridge_provide_results_dynamic(void *source,
                                        const uint16_t *count_ptr,
                                        uint8_t item_size) {
  s_data_source = source;
  s_item_count = 0;
  s_item_count_ptr = count_ptr;
  s_item_size = item_size;
}

// Async scan runner. A scan command posts its work function here and returns to
// the SPI handler immediately, so the blocking scan runs off the bridge and does
// not hold it for seconds. The P4 polls a *_SCAN_STATUS command until it clears.
// Only one scan runs at a time (the shared busy flag rejects overlaps).
static volatile bool s_scan_busy = false;
static void (*s_scan_fn)(void) = NULL;
static TaskHandle_t s_scan_runner = NULL;

static void scan_runner_task(void *arg) {
  (void)arg;
  while (1) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    void (*fn)(void) = s_scan_fn;
    if (fn != NULL) {
      fn();
    }
    s_scan_busy = false;
  }
}

bool spi_bridge_async_scan_start(void (*fn)(void)) {
  if (s_scan_busy) {
    return false;
  }
  if (s_scan_runner == NULL) {
    xTaskCreatePinnedToCore(scan_runner_task,
                            "scan_runner",
                            4096,
                            NULL,
                            SYS_PRIO_SERVICE_HI,
                            &s_scan_runner,
                            SYS_CORE_MAIN);
  }
  s_scan_fn = fn;
  s_scan_busy = true;
  xTaskNotifyGive(s_scan_runner);
  return true;
}

bool spi_bridge_async_scan_busy(void) {
  return s_scan_busy;
}

bool spi_bridge_stream_is_enabled(spi_id_t id) {
  if (id == SPI_ID_WIFI_APP_SNIFFER)
    return s_is_wifi_sniffer_streaming;
  if (id == SPI_ID_BT_APP_SNIFFER)
    return s_is_bt_sniffer_streaming;
  if (id == SPI_ID_MESH_TORADIO_STREAM)
    return s_is_mesh_toradio_streaming;
  if (id == SPI_ID_MCORE_RX_STREAM)
    return s_is_mcore_rx_streaming;
  if (id == SPI_ID_HOST_RX)
    return s_is_host_rx_streaming;
  if (id == SPI_ID_SYSTEM_LOG)
    return s_is_system_log_streaming;
  return false;
}

void spi_bridge_stream_enable(spi_id_t id, bool enable) {
  if (id == SPI_ID_WIFI_APP_SNIFFER)
    s_is_wifi_sniffer_streaming = enable;
  if (id == SPI_ID_BT_APP_SNIFFER)
    s_is_bt_sniffer_streaming = enable;
  if (id == SPI_ID_MESH_TORADIO_STREAM)
    s_is_mesh_toradio_streaming = enable;
  if (id == SPI_ID_MCORE_RX_STREAM)
    s_is_mcore_rx_streaming = enable;
  if (id == SPI_ID_HOST_RX)
    s_is_host_rx_streaming = enable;
  if (id == SPI_ID_SYSTEM_LOG)
    s_is_system_log_streaming = enable;
}

bool spi_bridge_stream_push(spi_id_t id, const uint8_t *data, uint8_t len) {
  if (!spi_bridge_stream_is_enabled(id))
    return false;
  if (data == NULL || len == 0)
    return false;
  // len is a uint8_t (<= 255 == SPI_MAX_PAYLOAD), so it always fits item->data.

  portENTER_CRITICAL(&s_stream_mux);
  if (s_stream_count >= SPI_STREAM_QUEUE_LEN) {
    portEXIT_CRITICAL(&s_stream_mux);
    return false;
  }
  spi_stream_item_t *item = &s_stream_queue[s_stream_tail];
  item->id = id;
  item->len = len;
  memcpy(item->data, data, len);
  s_stream_tail = (uint8_t)((s_stream_tail + 1) % SPI_STREAM_QUEUE_LEN);
  s_stream_count++;
  portEXIT_CRITICAL(&s_stream_mux);

  return true;
}

void spi_bridge_notify_master(void) {
  // POLL mode has no IRQ trace: the master polls the bus, so skip the pulse.
  if (!s_use_irq) {
    return;
  }
  // The P4 captures the IRQ via a GPIO rising-edge interrupt, so it only needs
  // a clean edge — not a held level. A short microsecond pulse replaces the old
  // 1 ms task delay, which dominated per-frame latency and capped stream rate.
  spi_slave_driver_set_irq(1);
  esp_rom_delay_us(SPI_IRQ_PULSE_US);
  spi_slave_driver_set_irq(0);
}

static void enter_download_mode(void) {
  ESP_LOGW(TAG, "Entering ROM serial download mode (force)");
  // On ESP32-C5 the force-download-boot selector lives in LP_AON_SYS_CFG_REG
  // bits 29-30. Value 0b01 = force download boot (uart/usb): the ROM bootloader
  // skips the app and stays in the serial-download stub on USB-Serial/JTAG,
  // listening for esptool. This is the cleanest software trigger on a board
  // with no hardware BOOT trace (used with SPI_ID_SYSTEM_ENTER_DOWNLOAD).
  uint32_t v = REG_READ(LP_AON_SYS_CFG_REG);
  v &= ~(LP_AON_FORCE_DOWNLOAD_BOOT_M);
  v |= (0x1U << LP_AON_FORCE_DOWNLOAD_BOOT_S);
  REG_WRITE(LP_AON_SYS_CFG_REG, v);
  vTaskDelay(pdMS_TO_TICKS(20)); // flush the log line before the reset
  esp_restart();
}

esp_err_t spi_bridge_slave_init(void) {
  return spi_bridge_slave_init_mode(SPI_BRIDGE_MODE_IRQ);
}

esp_err_t spi_bridge_slave_init_mode(spi_bridge_mode_t mode) {
  s_use_irq = (mode == SPI_BRIDGE_MODE_IRQ);
  esp_err_t ret = spi_slave_driver_init();
  if (ret != ESP_OK)
    return ret;
  session_manager_init();
  xTaskCreate(
      bridge_task, "spi_bridge_task", SPI_BRIDGE_TASK_STACK, NULL, SPI_BRIDGE_TASK_PRIO, NULL);
  return ESP_OK;
}

// Static functions

static void load_firmware_version(void) {
  strncpy(s_firmware_version, SPI_FW_VERSION_STRING, sizeof(s_firmware_version) - 1);
  s_firmware_version[sizeof(s_firmware_version) - 1] = '\0';
  ESP_LOGI(TAG, "Firmware version: %s", s_firmware_version);
}

// Pop the head stream item into buf at `offset`, encoded as a record
// [u16 op][u8 len][len bytes]. Returns the number of bytes written, or 0 if the
// queue is empty or the record would not fit in `cap`. Keeps the critical
// section short (one item, <=256 bytes) so the producer is never blocked long.
static uint16_t stream_pop_into(uint8_t *buf, uint16_t offset, uint16_t cap) {
  uint16_t written = 0;
  portENTER_CRITICAL(&s_stream_mux);
  if (s_stream_count > 0) {
    spi_stream_item_t *item = &s_stream_queue[s_stream_head];
    uint16_t need = 3u + item->len; // u16 op + u8 len + data
    if ((uint32_t)offset + need <= cap) {
      buf[offset] = (uint8_t)(item->id & 0xFF);
      buf[offset + 1] = (uint8_t)((item->id >> 8) & 0xFF);
      buf[offset + 2] = item->len;
      if (item->len > 0)
        memcpy(buf + offset + 3, item->data, item->len);
      written = need;
      s_stream_head = (uint8_t)((s_stream_head + 1) % SPI_STREAM_QUEUE_LEN);
      s_stream_count--;
    }
  }
  portEXIT_CRITICAL(&s_stream_mux);
  return written;
}

static void bridge_task(void *pvParameters) {
  // Static (this is the only task touching them) so the larger stream TX buffer
  // does not blow the task stack. RX/command stays at SPI_FRAME_SIZE; only the
  // stream response uses the larger SPI_STREAM_FRAME_SIZE buffer.
  static uint8_t rx_buf[SPI_FRAME_SIZE];
  static uint8_t tx_buf[SPI_STREAM_FRAME_SIZE];
  spi_slave_transaction_t rx_trans;
  spi_slave_transaction_t tx_trans;

  // Keep a receive transaction armed in hardware at all times. The next command
  // RX is re-armed right after the response TX is queued (below), so the master
  // can never clock a command into an unarmed slave — even if this task is
  // preempted between transfers.
  memset(rx_buf, 0, sizeof(rx_buf));
  if (spi_slave_driver_queue(&rx_trans, NULL, rx_buf, SPI_FRAME_SIZE) != ESP_OK) {
    vTaskDelete(NULL);
    return;
  }

  while (1) {
    if (spi_slave_driver_wait() != ESP_OK) {
      memset(rx_buf, 0, sizeof(rx_buf));
      spi_slave_driver_queue(&rx_trans, NULL, rx_buf, SPI_FRAME_SIZE);
      continue;
    }

    spi_header_t *header = (spi_header_t *)rx_buf;
    // Drop framing-invalid or bus-corrupted commands: bad sync/type, or a CRC
    // mismatch. Dropping (rather than acting) means the master gets no response
    // and its command times out, so a corrupted op/payload is never executed.
    // No length clause: header->length is a uint8_t (<= 255 == SPI_MAX_PAYLOAD),
    // so it always fits rx_buf and the CRC recompute stays in bounds.
    if (header->sync != SPI_SYNC_BYTE || header->type != SPI_TYPE_CMD ||
        !spi_frame_valid(header, header->length)) {
      memset(rx_buf, 0, sizeof(rx_buf));
      spi_slave_driver_queue(&rx_trans, NULL, rx_buf, SPI_FRAME_SIZE);
      continue;
    }

    spi_status_t status = SPI_STATUS_OK;
    uint8_t resp_payload[SPI_MAX_PAYLOAD];
    uint8_t resp_len = 0;
    bool tx_ready = false;           // set when the case already built a complete tx_buf frame
    size_t tx_size = SPI_FRAME_SIZE; // bytes the master will clock for the response

    uint16_t cmd = spi_header_cmd(header);
    const uint8_t *cmd_payload = rx_buf + sizeof(spi_header_t);

    switch (header->category) {
      case SPI_CAT_SYSTEM:
        if (cmd == SPI_ID_SYSTEM_PING) {
          status = SPI_STATUS_OK;
        } else if (cmd == SPI_ID_SYSTEM_REBOOT) {
          status = SPI_STATUS_OK;
          s_is_restart_pending = true;
        } else if (cmd == SPI_ID_SYSTEM_ENTER_DOWNLOAD) {
          // Ack first, then reboot into ROM download mode after the response
          // transfer completes (deferred, like reboot) so the P4 sees the OK.
          status = SPI_STATUS_OK;
          s_is_download_pending = true;
        } else if (cmd == SPI_ID_SYSTEM_OTA_BEGIN) {
          // Read {size, transport} and spawn the receiver task. Non-blocking: the
          // P4 polls OTA_STATUS for READY, then sends the image over SPI (OTA_DATA)
          // or UART depending on transport.
          if (header->length >= sizeof(spi_ota_begin_t)) {
            spi_ota_begin_t req;
            memcpy(&req, cmd_payload, sizeof(req));
            status = (ota_service_begin(req.size, req.transport) == ESP_OK) ? SPI_STATUS_OK
                                                                            : SPI_STATUS_ERROR;
          } else {
            status = SPI_STATUS_INVALID_ARG;
          }
        } else if (cmd == SPI_ID_SYSTEM_INFO) {
          esp_chip_info_t ci;
          esp_chip_info(&ci);
          spi_sys_info_t info = {0};
          info.chip_model = (uint8_t)ci.model;
          info.chip_revision = (uint16_t)ci.revision;
          esp_read_mac(info.mac, ESP_MAC_WIFI_STA);
          info.free_heap = esp_get_free_heap_size();
          memcpy(resp_payload, &info, sizeof(info));
          resp_len = sizeof(info);
          status = SPI_STATUS_OK;
        } else if (cmd == SPI_ID_SYSTEM_PROTO_VERSION) {
          // Report our wire-protocol version so the P4 can detect a drifted
          // spi_protocol.h copy at boot (see bridge_manager check_c5_protocol).
          uint16_t proto = SPI_PROTOCOL_VERSION;
          memcpy(resp_payload, &proto, sizeof(proto));
          resp_len = sizeof(proto);
          status = SPI_STATUS_OK;
        } else if (cmd == SPI_ID_SYSTEM_POWER_STATE) {
          // P4 tells us the device power state so we can drop the radio when it
          // is idle/asleep. A running capture (promiscuous) is never interrupted.
          if (header->length >= 1) {
            switch (cmd_payload[0]) {
              case SPI_POWER_ACTIVE:
                if (!wifi_service_is_active()) {
                  wifi_service_start();
                }
                wifi_service_set_power_save(false);
                break;
              case SPI_POWER_IDLE:
                wifi_service_set_power_save(true);
                break;
              case SPI_POWER_SLEEP:
                if (!wifi_service_is_busy() && wifi_service_is_active()) {
                  wifi_service_stop();
                }
                break;
              default:
                break;
            }
            status = SPI_STATUS_OK;
          } else {
            status = SPI_STATUS_INVALID_ARG;
          }
        } else if (cmd == SPI_ID_SYSTEM_OTA_DATA) {
          // One firmware chunk. Buffered (fast) and acked at once; the writer
          // task drains it to flash. BUSY means the writer is behind - the P4
          // retries the same chunk.
          esp_err_t wr = ota_service_write(cmd_payload, header->length);
          status = (wr == ESP_OK)              ? SPI_STATUS_OK
                   : (wr == ESP_ERR_NO_MEM)    ? SPI_STATUS_BUSY
                                               : SPI_STATUS_ERROR;
        } else if (cmd == SPI_ID_SYSTEM_OTA_STATUS) {
          spi_ota_status_t ota_st;
          ota_service_get_status(&ota_st);
          memcpy(resp_payload, &ota_st, sizeof(ota_st));
          resp_len = sizeof(ota_st);
          status = SPI_STATUS_OK;
        } else if (cmd == SPI_ID_SYSTEM_VERSION) {
          if (strcmp(s_firmware_version, "unknown") == 0)
            load_firmware_version();
          size_t ver_len = strlen(s_firmware_version);
          if (ver_len > (SPI_MAX_PAYLOAD - SPI_RESP_STATUS_SIZE))
            ver_len = (SPI_MAX_PAYLOAD - SPI_RESP_STATUS_SIZE);
          memcpy(resp_payload, s_firmware_version, ver_len);
          resp_len = (uint8_t)ver_len;
          status = SPI_STATUS_OK;
        } else if (cmd == SPI_ID_SYSTEM_STATUS) {
          spi_system_status_t sys = {.wifi_active = wifi_service_is_active() ? 1 : 0,
                                     .wifi_connected = wifi_service_is_connected() ? 1 : 0,
                                     .bt_running = bluetooth_service_is_running() ? 1 : 0,
                                     .bt_initialized = bluetooth_service_is_initialized() ? 1 : 0};
          memcpy(resp_payload, &sys, sizeof(sys));
          resp_len = sizeof(sys);
          status = SPI_STATUS_OK;
        } else if (cmd == SPI_ID_SYSTEM_DATA) {
          uint16_t index;
          if (header->length < sizeof(index)) {
            status = SPI_STATUS_INVALID_ARG; // truncated frame - do not read past it
            break;
          }
          memcpy(&index, cmd_payload, sizeof(index));
          uint16_t item_count = s_item_count_ptr != NULL ? *s_item_count_ptr : s_item_count;

          if (index == SPI_DATA_INDEX_COUNT) {
            memcpy(resp_payload, &item_count, sizeof(item_count));
            resp_len = sizeof(item_count);
          } else if (index == SPI_DATA_INDEX_STATS) {
            spi_sniffer_stats_t stats = {.packets = wifi_sniffer_get_packet_count(),
                                         .deauths = wifi_sniffer_get_deauth_count(),
                                         .buffer_usage = wifi_sniffer_get_buffer_usage(),
                                         .signal_rssi = signal_monitor_get_rssi(),
                                         .handshake_captured = wifi_sniffer_handshake_captured(),
                                         .pmkid_captured = wifi_sniffer_pmkid_captured()};
            wifi_sniffer_fill_ext_stats(&stats);
            memcpy(resp_payload, &stats, sizeof(stats));
            resp_len = sizeof(stats);
          } else if (index == SPI_DATA_INDEX_DEAUTH_COUNT) {
            uint32_t deauth_count = deauther_detector_get_count();
            memcpy(resp_payload, &deauth_count, sizeof(deauth_count));
            resp_len = sizeof(deauth_count);
          } else if (s_data_source != NULL && index < item_count) {
            memcpy(resp_payload, (uint8_t *)s_data_source + (index * s_item_size), s_item_size);
            resp_len = s_item_size;
          } else {
            status = SPI_STATUS_ERROR;
          }
        } else if (cmd == SPI_ID_SYSTEM_STREAM) {
          // Batch as many queued records as fit into one large stream frame:
          //   [header type=STREAM][u16 batch_len][u16 op][u8 len][data]...
          // The master always clocks SPI_STREAM_FRAME_SIZE for stream reads;
          // batch_len = 0 means "no data" and the P4 just backs off.
          uint8_t *recs = tx_buf + sizeof(spi_header_t) + sizeof(uint16_t);
          uint16_t cap = SPI_STREAM_FRAME_SIZE - sizeof(spi_header_t) - sizeof(uint16_t);
          uint16_t batch_len = 0;
          uint16_t w;
          while ((w = stream_pop_into(recs, batch_len, cap)) > 0)
            batch_len += w;

          spi_header_t stream_header = {
              .sync = SPI_SYNC_BYTE, .type = SPI_TYPE_STREAM, .category = 0, .op = 0, .length = 0};
          memcpy(tx_buf, &stream_header, sizeof(stream_header));
          tx_buf[sizeof(spi_header_t)] = (uint8_t)(batch_len & 0xFF);
          tx_buf[sizeof(spi_header_t) + 1] = (uint8_t)((batch_len >> 8) & 0xFF);
          spi_frame_seal((spi_header_t *)tx_buf, (uint16_t)(sizeof(uint16_t) + batch_len));
          tx_size = SPI_STREAM_FRAME_SIZE;
          tx_ready = true;
        } else {
          status = SPI_STATUS_UNSUPPORTED;
        }
        break;
      case SPI_CAT_SESSION:
        if (cmd == SPI_ID_SESSION_HEARTBEAT) {
          spi_heartbeat_req_t req = {0};
          if (header->length < sizeof(req)) {
            status = SPI_STATUS_INVALID_ARG; // truncated frame - do not read past it
            break;
          }
          memcpy(&req, cmd_payload, sizeof(req));
          bool alive = session_manager_heartbeat(req.session_id, req.last_acked_seq);
          spi_heartbeat_resp_t resp = {.alive = alive ? (uint8_t)1 : (uint8_t)0};
          memcpy(resp_payload, &resp, sizeof(resp));
          resp_len = sizeof(resp);
          status = SPI_STATUS_OK;
        } else if (cmd == SPI_ID_SESSION_STOP) {
          spi_session_stop_req_t req = {0};
          if (header->length < sizeof(req)) {
            status = SPI_STATUS_INVALID_ARG; // truncated frame - do not read past it
            break;
          }
          memcpy(&req, cmd_payload, sizeof(req));
          esp_err_t r = session_manager_stop(req.session_id);
          status = (r == ESP_OK) ? SPI_STATUS_OK : SPI_STATUS_ERROR;
        } else {
          status = SPI_STATUS_UNSUPPORTED;
        }
        break;
      case SPI_CAT_WIFI:
        status = wifi_dispatcher_execute(cmd, cmd_payload, header->length, resp_payload, &resp_len);
        break;
      case SPI_CAT_BT:
      case SPI_CAT_MCORE:
      case SPI_CAT_HOST:
        status = bt_dispatcher_execute(cmd, cmd_payload, header->length, resp_payload, &resp_len);
        break;
      case SPI_CAT_MESH:
        // Meshtastic is split across dispatchers by transport: the WiFi
        // transport ops live in the WiFi dispatcher, the rest (BLE, fromradio,
        // log, status) in the BT dispatcher.
        if (cmd == SPI_ID_MESH_WIFI_INIT || cmd == SPI_ID_MESH_WIFI_STOP) {
          status =
              wifi_dispatcher_execute(cmd, cmd_payload, header->length, resp_payload, &resp_len);
        } else {
          status = bt_dispatcher_execute(cmd, cmd_payload, header->length, resp_payload, &resp_len);
        }
        break;
      default:
        status = SPI_STATUS_UNSUPPORTED;
        break;
    }

    if (!tx_ready) {
      if (resp_len > (SPI_MAX_PAYLOAD - SPI_RESP_STATUS_SIZE)) {
        resp_len = 0;
        status = SPI_STATUS_ERROR;
      }

      spi_header_t resp_header = {.sync = SPI_SYNC_BYTE,
                                  .type = SPI_TYPE_RESP,
                                  .category = header->category,
                                  .op = header->op,
                                  .length = (uint8_t)(resp_len + SPI_RESP_STATUS_SIZE)};
      memset(tx_buf, 0, SPI_FRAME_SIZE);
      memcpy(tx_buf, &resp_header, sizeof(resp_header));
      tx_buf[sizeof(resp_header)] = (uint8_t)status;
      if (resp_len > 0)
        memcpy(tx_buf + sizeof(resp_header) + SPI_RESP_STATUS_SIZE, resp_payload, resp_len);
      spi_frame_seal((spi_header_t *)tx_buf, resp_header.length);
    }

    // Arm the response, signal the master, then immediately re-arm the next
    // receive so the slave is ready before the response transfer even completes.
    // tx_size is SPI_STREAM_FRAME_SIZE for a batched stream frame, else SPI_FRAME_SIZE.
    spi_slave_driver_queue(&tx_trans, tx_buf, NULL, tx_size);
    spi_bridge_notify_master();
    memset(rx_buf, 0, sizeof(rx_buf));
    spi_slave_driver_queue(&rx_trans, NULL, rx_buf, SPI_FRAME_SIZE);

    spi_slave_driver_wait(); // wait for the response transfer to complete

    if (s_is_download_pending) {
      enter_download_mode();
    }
    if (s_is_restart_pending) {
      vTaskDelay(pdMS_TO_TICKS(SPI_RESTART_DELAY_MS));
      esp_restart();
    }
  }
}
