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

#include "ethernet_share.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "battery_service.h"
#include "bq25896.h"
#include "husb320.h"
#include "led_control.h"
#include "power_manager.h"
#include "spi_bridge.h"
#include "sys_prio.h"
#include "tusb_desc.h"
#include "usb_ethernet.h"
#include "ethernet_power_profiles.h"
#include "wifi_hotspot_preset.h"
#include "wifi_service.h"

#define WORKER_STACK            6144
#define POLL_MS                 20
#define IDLE_POLL_MS            2
#define BUSY_RETRY_COUNT        10
#define RX_TIMEOUT_LIMIT        3
#define SPI_TIMEOUT_MS          500
#define TRANSPORT_YIELD_MS      1
#define STATUS_PERIOD_US        500000
#define BOOST_CHECK_US          1000000
#define ATTACHMENT_PERIOD_US    50000
#define START_RETRY_US          5000000
#define BOOST_MIN_MV            3500
#define ENUMERATION_TIMEOUT_US  INT64_C(20000000)
#define INTERNAL_HEADROOM_BYTES 10240

static const char *TAG = "ETH_SHARE";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static ethernet_share_snapshot_t s_snapshot;
static atomic_bool s_running, s_stop;

const char *ethernet_share_stage_name(ethernet_share_stage_t stage) {
  static const char *const names[ETHERNET_SHARE_STAGE_COUNT] = {"Power lease",
                                                                "USB ownership",
                                                                "USB-C source role",
                                                                "USB host driver",
                                                                "USB routing",
                                                                "USB-C attachment",
                                                                "USB power output",
                                                                "Battery/boost check",
                                                                "Adapter setup",
                                                                "C5 radio reservation",
                                                                "C5 DHCP/router",
                                                                "Packet forwarding",
                                                                "Forwarding to C5",
                                                                "Reading packets from C5",
                                                                "USB packet transmission",
                                                                "Hotspot preset"};
  return stage < ETHERNET_SHARE_STAGE_COUNT && stage >= 0 ? names[stage] : "Unknown";
}
static void set_stage(ethernet_share_stage_t stage) {
  portENTER_CRITICAL(&s_lock);
  s_snapshot.stage = stage;
  portEXIT_CRITICAL(&s_lock);
}
static void state(ethernet_share_state_t next, esp_err_t error) {
  portENTER_CRITICAL(&s_lock);
  bool changed = s_snapshot.state != next || s_snapshot.error != error;
  s_snapshot.state = next;
  s_snapshot.error = error;
  ethernet_share_stage_t stage = s_snapshot.stage;
  portEXIT_CRITICAL(&s_lock);
  if (!changed)
    return;
  if (error != ESP_OK) {
    ESP_LOGE(TAG, "%s failed: %s", ethernet_share_stage_name(stage), esp_err_to_name(error));
    led_signal_error();
  } else if (next == ETHERNET_SHARE_ONLINE) {
    ESP_LOGI(TAG, "Ethernet hotspot sharing active");
    led_signal_info();
  }
}
static esp_err_t command(
    spi_id_t id, const void *data, size_t length, void *out, size_t capacity, size_t *out_length) {
  if (out_length)
    *out_length = 0;
  for (unsigned attempt = 0; attempt < BUSY_RETRY_COUNT; ++attempt) {
    spi_header_t header = {0};
    esp_err_t ret =
        spi_bridge_send_command(id, data, length, &header, out, capacity, SPI_TIMEOUT_MS);
    // Give board leases and Companion a scheduling point between Ethernet commands.
    vTaskDelay(pdMS_TO_TICKS(TRANSPORT_YIELD_MS));
    bool peer_busy = ret == ESP_ERR_INVALID_STATE && header.type == SPI_TYPE_RESP &&
                     spi_header_cmd(&header) == id;
    if (!peer_busy) {
      if (out_length && ret == ESP_OK)
        *out_length = header.length;
      return ret;
    }
    // A full RX queue may already have consumed the final fragment: never replay it.
    if (id == SPI_ID_ETHERNET_RX || id == SPI_ID_ETHERNET_START)
      return ESP_ERR_NOT_FINISHED;
    if (atomic_load(&s_stop) && id != SPI_ID_ETHERNET_STOP)
      return ESP_ERR_INVALID_STATE;
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  }
  return ESP_ERR_TIMEOUT;
}
static bool recover_exchange_timeout(esp_err_t result, unsigned *timeouts) {
  if (result == ESP_OK)
    *timeouts = 0;
  if (result != ESP_ERR_TIMEOUT)
    return false;
  return ++(*timeouts) < RX_TIMEOUT_LIMIT;
}

static void worker(void *unused) {
  (void)unused;
  uint8_t *rx = NULL, *tx = NULL;
  bool phy = false, awake = false, host = false, role = false, powered = false, routed = false;
  uint32_t token = 0;
  unsigned exchange_timeouts = 0;
  uint16_t applied_current_ma = ETHERNET_POWER_STARTUP_MA;
  esp_err_t ret = power_manager_no_sleep_acquire();
  if (ret != ESP_OK)
    goto cleanup;
  awake = true;
  set_stage(ETHERNET_SHARE_STAGE_PRESET);
  if (wifi_service_is_active()) {
    ret = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }
  ret = wifi_service_save_ap_config(WIFI_HOTSPOT_PRESET_SSID,
                                    WIFI_HOTSPOT_PRESET_PASSWORD,
                                    WIFI_HOTSPOT_PRESET_MAX_CLIENTS,
                                    WIFI_HOTSPOT_PRESET_IP,
                                    false);
  if (ret != ESP_OK)
    goto cleanup;
  set_stage(ETHERNET_SHARE_STAGE_ROUTER_START);
  uint32_t capabilities = 0;
  size_t capability_bytes = 0;
  ret = command(SPI_ID_ETHERNET_CAPABILITIES,
                NULL,
                0,
                &capabilities,
                sizeof(capabilities),
                &capability_bytes);
  if (ret != ESP_OK)
    goto cleanup;
  if (capability_bytes != sizeof(capabilities) || !(capabilities & SPI_ETHERNET_CAP_PACKET)) {
    ret = ESP_ERR_NOT_SUPPORTED;
    goto cleanup;
  }
  set_stage(ETHERNET_SHARE_STAGE_USB_CLAIM);
  phy = busb_host_claim();
  if (!phy) {
    ret = ESP_ERR_INVALID_STATE;
    goto cleanup;
  }
  // Device teardown may have left the connector routed native before host startup.
  routed = true;
  rx = heap_caps_malloc(SPI_ETHERNET_MTU, MALLOC_CAP_SPIRAM);
  tx = heap_caps_malloc(SPI_ETHERNET_MTU, MALLOC_CAP_SPIRAM);
  if (rx == NULL || tx == NULL) {
    ESP_LOGE(TAG, "Frame buffer allocation failed");
    ret = ESP_ERR_NO_MEM;
    goto cleanup;
  }
  state(ETHERNET_SHARE_UNPLUG, ESP_OK);
  while (!atomic_load(&s_stop)) {
    battery_snapshot_t battery;
    if (battery_service_get(&battery) && !battery.vbus_present)
      break;
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  }
  if (atomic_load(&s_stop))
    goto cleanup;
  set_stage(ETHERNET_SHARE_STAGE_TYPEC);
  ret = husb320_set_host_mode(true);
  role = husb320_host_mode();
  if (ret != ESP_OK)
    goto cleanup;
  host = true;
  set_stage(ETHERNET_SHARE_STAGE_USB_DRIVER);
  ret = usb_ethernet_start();
  if (ret != ESP_OK)
    goto cleanup;
  routed = true;
  set_stage(ETHERNET_SHARE_STAGE_USB_ROUTE);
  ret = usb_mux_set_host(true);
  if (ret != ESP_OK)
    goto cleanup;
  state(ETHERNET_SHARE_WAIT_ADAPTER, ESP_OK);
  int64_t next_status = 0, next_power = 0, start_deadline = 0, enumeration_deadline = 0;
  int64_t next_attachment = 0;
  bool attached = false;
  while (!atomic_load(&s_stop)) {
    if (esp_timer_get_time() >= next_attachment) {
      set_stage(ETHERNET_SHARE_STAGE_ATTACHMENT);
      ret = husb320_host_attached(&attached);
      if (ret != ESP_OK)
        break;
      next_attachment = esp_timer_get_time() + ATTACHMENT_PERIOD_US;
    }
    if (!usb_mux_is_native()) {
      set_stage(ETHERNET_SHARE_STAGE_USB_ROUTE);
      ret = ESP_ERR_INVALID_STATE;
      break;
    }
    if (attached && !powered) {
      set_stage(ETHERNET_SHARE_STAGE_USB_POWER);
      ret = bq25896_host_power_with_limit(true, ETHERNET_POWER_STARTUP_MA);
      if (ret != ESP_OK)
        break;
      powered = true;
      applied_current_ma = ETHERNET_POWER_STARTUP_MA;
      enumeration_deadline = esp_timer_get_time() + ENUMERATION_TIMEOUT_US;
      ESP_LOGI(TAG, "USB host startup current limit: %u mA", applied_current_ma);
      state(ETHERNET_SHARE_WAIT_ADAPTER, ESP_OK);
    } else if (!attached && powered) {
      set_stage(ETHERNET_SHARE_STAGE_USB_POWER);
      ret = bq25896_host_power(false);
      powered = false;
      if (ret != ESP_OK)
        break;
      state(ETHERNET_SHARE_WAIT_ADAPTER, ESP_OK);
    }
    int64_t now = esp_timer_get_time();
    if (powered && now >= next_power) {
      set_stage(ETHERNET_SHARE_STAGE_POWER_CHECK);
      battery_charge_snapshot_t battery = {0};
      bool is_valid = battery_service_get_charge(&battery) && battery.valid;
      const char *reason = NULL;
      if (!is_valid)
        reason = battery.status != NULL ? battery.status : "Battery snapshot unavailable";
      else if (!battery.charger.adc_valid)
        reason = "Battery ADC unavailable";
      else if (battery.charger.vbat_mv < BOOST_MIN_MV)
        reason = "Battery below USB power minimum";
      else if (battery.charger.boost_fault)
        reason = "Boost fault detected";
      else if (battery.charger.thermal_regulation)
        reason = "Charger thermal regulation";
      if (reason != NULL) {
        char detail[ETHERNET_SHARE_POWER_DETAIL_SIZE] = {0};
        snprintf(detail,
                 sizeof(detail),
                 "%s\nVBAT: %u mV (min %u)\nFault: %02X/%02X",
                 reason,
                 (unsigned)battery.charger.vbat_mv,
                 BOOST_MIN_MV,
                 battery.charger.fault_latched,
                 battery.charger.fault_live);
        portENTER_CRITICAL(&s_lock);
        memcpy(s_snapshot.power_detail, detail, sizeof(detail));
        portEXIT_CRITICAL(&s_lock);
        ESP_LOGE(TAG,
                 "Power guard: %s; valid=%d adc=%d VBAT=%u mV "
                 "fault latched/live=%02X/%02X boost=%d thermal=%d OTG=%d",
                 reason,
                 is_valid,
                 battery.charger.adc_valid,
                 (unsigned)battery.charger.vbat_mv,
                 battery.charger.fault_latched,
                 battery.charger.fault_live,
                 battery.charger.boost_fault,
                 battery.charger.thermal_regulation,
                 battery.charger.otg_enabled);
        ret = ESP_ERR_INVALID_STATE;
        break;
      }
      next_power = now + BOOST_CHECK_US;
    }
    set_stage(ETHERNET_SHARE_STAGE_ADAPTER);
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < INTERNAL_HEADROOM_BYTES) {
      ret = ESP_ERR_NO_MEM;
      break;
    }
    ret = usb_ethernet_poll();
    if (ret != ESP_OK)
      break;
    uint16_t vid = 0, pid = 0;
    uint16_t desired_current_ma = ETHERNET_POWER_STARTUP_MA;
    bool identified = usb_ethernet_identity(&vid, &pid);
    if (identified) {
      enumeration_deadline = 0;
      desired_current_ma = ETHERNET_POWER_DEFAULT_MA;
      for (size_t i = 0;
           i < sizeof(s_ethernet_power_profiles) / sizeof(s_ethernet_power_profiles[0]);
           ++i) {
        if (s_ethernet_power_profiles[i].vid == vid && s_ethernet_power_profiles[i].pid == pid) {
          desired_current_ma = s_ethernet_power_profiles[i].current_ma;
          break;
        }
      }
    }
    if (powered && !identified) {
      if (enumeration_deadline == 0)
        enumeration_deadline = now + ENUMERATION_TIMEOUT_US;
      if (now >= enumeration_deadline) {
        ESP_LOGE(TAG, "USB adapter enumeration timed out");
        ret = ESP_ERR_TIMEOUT;
        break;
      }
    }
    if (powered && desired_current_ma != applied_current_ma) {
      set_stage(ETHERNET_SHARE_STAGE_USB_POWER);
      ret = bq25896_host_set_current_limit(desired_current_ma);
      if (ret != ESP_OK)
        break;
      applied_current_ma = desired_current_ma;
      ESP_LOGI(
          TAG, "USB adapter %04x:%04x current limit request: %u mA", vid, pid, desired_current_ma);
    }
    spi_ethernet_start_t request = {0};
    bool link = powered && usb_ethernet_link(request.mac);
    if (!link) {
      if (token != 0) {
        (void)command(SPI_ID_ETHERNET_STOP, &token, sizeof(token), NULL, 0, NULL);
        token = 0;
        exchange_timeouts = 0;
      }
      start_deadline = 0;
      if (powered)
        state(identified ? ETHERNET_SHARE_WAIT_LINK : ETHERNET_SHARE_WAIT_ADAPTER, ESP_OK);
      vTaskDelay(pdMS_TO_TICKS(POLL_MS));
      continue;
    }
    if (token == 0) {
      set_stage(ETHERNET_SHARE_STAGE_ROUTER_START);
      request.token = esp_random();
      if (request.token == 0)
        request.token = 1;
      ret = command(SPI_ID_ETHERNET_START, &request, sizeof(request), NULL, 0, NULL);
      if (ret == ESP_ERR_NOT_FINISHED) {
        if (start_deadline == 0)
          start_deadline = now + START_RETRY_US;
        if (now < start_deadline) {
          vTaskDelay(pdMS_TO_TICKS(POLL_MS));
          continue;
        }
      }
      if (ret != ESP_OK)
        break;
      token = request.token;
      state(ETHERNET_SHARE_DHCP, ESP_OK);
    }
    if (now >= next_status) {
      set_stage(ETHERNET_SHARE_STAGE_ROUTER_STATUS);
      spi_ethernet_status_t status;
      size_t bytes = 0;
      ret = command(SPI_ID_ETHERNET_STATUS, &token, sizeof(token), &status, sizeof(status), &bytes);
      if (ret != ESP_OK)
        break;
      if (bytes != sizeof(status) || status.token != token) {
        ret = ESP_ERR_INVALID_RESPONSE;
        break;
      }
      if (status.state == SPI_ETHERNET_STATE_ERROR) {
        ret = status.error;
        break;
      }
      portENTER_CRITICAL(&s_lock);
      s_snapshot.ip = status.ip;
      memcpy(s_snapshot.ssid, status.ssid, sizeof(s_snapshot.ssid));
      s_snapshot.ssid[sizeof(s_snapshot.ssid) - 1] = 0;
      portEXIT_CRITICAL(&s_lock);
      if (status.state == SPI_ETHERNET_STATE_SHARING)
        state(ETHERNET_SHARE_ONLINE, ESP_OK);
      next_status = now + STATUS_PERIOD_US;
    }
    set_stage(ETHERNET_SHARE_STAGE_FORWARDING);
    size_t bytes = usb_ethernet_receive(rx);
    spi_bridge_ethernet_exchange_t exchange = {.token = token,
                                               .tx_data = rx,
                                               .tx_length = bytes,
                                               .rx_data = tx,
                                               .rx_capacity = SPI_ETHERNET_MTU};
    ret = spi_bridge_ethernet_exchange(&exchange, SPI_TIMEOUT_MS);
    vTaskDelay(pdMS_TO_TICKS(TRANSPORT_YIELD_MS));
    if (atomic_load(&s_stop))
      break;
    if (recover_exchange_timeout(ret, &exchange_timeouts)) {
      if (exchange_timeouts == 1) {
        ESP_LOGW(TAG, "C5 packet exchange timed out; checking router status");
        led_signal_warning();
      }
      // Either packet may already have been consumed. Never replay after timeout.
      next_status = 0;
      ret = ESP_OK;
      vTaskDelay(pdMS_TO_TICKS(POLL_MS));
      continue;
    }
    if (ret == ESP_ERR_NOT_FINISHED) {
      vTaskDelay(pdMS_TO_TICKS(IDLE_POLL_MS));
      continue;
    }
    if (ret != ESP_OK)
      break;
    if (exchange.tx_accepted && bytes != 0) {
      portENTER_CRITICAL(&s_lock);
      s_snapshot.received += bytes;
      portEXIT_CRITICAL(&s_lock);
    }
    if (exchange.rx_length != 0) {
      set_stage(ETHERNET_SHARE_STAGE_USB_TX);
      ret = usb_ethernet_transmit(tx, exchange.rx_length);
      if (ret == ESP_ERR_INVALID_STATE && !usb_ethernet_link(request.mac)) {
        ret = ESP_OK;
        continue;
      }
      if (ret != ESP_OK)
        break;
      portENTER_CRITICAL(&s_lock);
      s_snapshot.transmitted += exchange.rx_length;
      portEXIT_CRITICAL(&s_lock);
    }
    if (bytes == 0 && exchange.rx_length == 0)
      vTaskDelay(pdMS_TO_TICKS(IDLE_POLL_MS));
  }
cleanup:
  if (ret != ESP_OK && !atomic_load(&s_stop))
    state(ETHERNET_SHARE_ERROR, ret);
  else
    state(ETHERNET_SHARE_STOPPING, ESP_OK);
  if (token != 0)
    (void)command(SPI_ID_ETHERNET_STOP, &token, sizeof(token), NULL, 0, NULL);
  // Release power before tearing down USB tasks; retry failed cleanup while retaining ownership.
  if (role) {
    while (bq25896_host_power(false) != ESP_OK)
      vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  }
  if (host)
    while (usb_ethernet_stop() != ESP_OK)
      vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  if (routed)
    while (usb_mux_set_host(false) != ESP_OK)
      vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  if (role)
    while (husb320_set_host_mode(false) != ESP_OK)
      vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  if (phy)
    busb_host_release();
  free(rx);
  free(tx);
  if (awake)
    power_manager_no_sleep_release();
  if (ret == ESP_OK || atomic_load(&s_stop))
    state(ETHERNET_SHARE_IDLE, ESP_OK);
  atomic_store(&s_running, false);
  vTaskDelete(NULL);
}

esp_err_t ethernet_share_start(void) {
  bool expected = false;
  if (!atomic_compare_exchange_strong(&s_running, &expected, true))
    return ESP_ERR_INVALID_STATE;
  atomic_store(&s_stop, false);
  portENTER_CRITICAL(&s_lock);
  s_snapshot = (ethernet_share_snapshot_t){0};
  portEXIT_CRITICAL(&s_lock);
  if (xTaskCreatePinnedToCore(
          worker, "eth_share", WORKER_STACK, NULL, SYS_PRIO_SERVICE_LO, NULL, SYS_CORE_RADIO) !=
      pdPASS) {
    atomic_store(&s_running, false);
    state(ETHERNET_SHARE_ERROR, ESP_ERR_NO_MEM);
    return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}
void ethernet_share_stop(void) {
  atomic_store(&s_stop, true);
}
void ethernet_share_get(ethernet_share_snapshot_t *out) {
  if (out == NULL)
    return;
  portENTER_CRITICAL(&s_lock);
  *out = s_snapshot;
  portEXIT_CRITICAL(&s_lock);
}
