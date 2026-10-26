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

#include "gatt_explorer.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "os/os_mbuf.h"

#include "bluetooth_service.h"

#define EXPLORER_LEASE_MS       60000
#define EXPLORER_LOCK_MS        1000
#define EXPLORER_CLOSE_RETRY_MS 1000
#define EXPLORER_ADDRESS_SIZE   6
#define EXPLORER_NO_CONNECTION  BLE_HS_CONN_HANDLE_NONE
#define EXPLORER_STOP_REASON    BLE_ERR_REM_USER_CONN_TERM
#define EXPLORER_UUID16_SIZE    2
#define EXPLORER_UUID32_SIZE    4

static const char *TAG = "GATT_EXPLORER";
static SemaphoreHandle_t s_lock;
static struct ble_npl_event s_command;
static struct ble_npl_callout s_lease;
static spi_gatt_status_t s_status;
static spi_gatt_record_t s_records[SPI_GATT_RECORD_MAX];
static uint16_t s_connection = EXPLORER_NO_CONNECTION;
static uint8_t s_address[EXPLORER_ADDRESS_SIZE], s_address_type;
static uint8_t s_service_index;
static uint16_t s_read_handle;
static uint32_t s_generation;
static bool s_has_start, s_has_stop, s_has_read, s_has_npl;

static int on_service(uint16_t connection,
                      const struct ble_gatt_error *error,
                      const struct ble_gatt_svc *service,
                      void *arg);
static int on_characteristic(uint16_t connection,
                             const struct ble_gatt_error *error,
                             const struct ble_gatt_chr *characteristic,
                             void *arg);
static int on_read(uint16_t connection,
                   const struct ble_gatt_error *error,
                   struct ble_gatt_attr *attribute,
                   void *arg);
static int on_gap(struct ble_gap_event *event, void *arg);
static void on_command(struct ble_npl_event *event);
static void on_lease(struct ble_npl_event *event);
static void discover_next(void);
static void fail(uint16_t error);
static void close_connection(void);
static void copy_uuid(spi_gatt_record_t *out, const ble_uuid_any_t *uuid);

bool gatt_explorer_start(const uint8_t *addr, uint8_t addr_type) {
  if (addr == NULL || addr_type > BLE_ADDR_RANDOM_ID || !bluetooth_service_is_running())
    return false;
  if (s_lock == NULL) {
    s_lock = xSemaphoreCreateRecursiveMutex();
    if (s_lock == NULL) {
      ESP_LOGE(TAG, "Cannot allocate GATT lock");
      return false;
    }
  }
  if (xSemaphoreTakeRecursive(s_lock, pdMS_TO_TICKS(EXPLORER_LOCK_MS)) != pdTRUE)
    return false;
  if (!s_has_npl) {
    ble_npl_event_init(&s_command, on_command, NULL);
    if (ble_npl_callout_init(&s_lease, nimble_port_get_dflt_eventq(), on_lease, NULL) != 0) {
      ble_npl_event_deinit(&s_command);
      xSemaphoreGiveRecursive(s_lock);
      ESP_LOGE(TAG, "Cannot allocate GATT lease timer");
      return false;
    }
    s_has_npl = true;
  }
  bool can_start = s_connection == EXPLORER_NO_CONNECTION &&
                   (s_status.state == SPI_GATT_IDLE || s_status.state == SPI_GATT_ERROR);
  if (can_start) {
    if (++s_generation == 0)
      ++s_generation;
    s_status = (spi_gatt_status_t){.state = SPI_GATT_CONNECTING};
    memset(s_records, 0, sizeof(s_records));
    memcpy(s_address, addr, sizeof(s_address));
    s_address_type = addr_type;
    s_has_start = true;
    s_has_stop = s_has_read = false;
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_command);
  }
  xSemaphoreGiveRecursive(s_lock);
  return can_start;
}

bool gatt_explorer_is_busy(void) {
  if (s_lock == NULL)
    return false;
  if (xSemaphoreTakeRecursive(s_lock, pdMS_TO_TICKS(EXPLORER_LOCK_MS)) != pdTRUE)
    return true;
  bool is_busy = (s_status.state != SPI_GATT_IDLE && s_status.state != SPI_GATT_ERROR) ||
                 s_has_start || s_has_stop || s_has_read;
  xSemaphoreGiveRecursive(s_lock);
  return is_busy;
}

esp_err_t gatt_explorer_deinit(void) {
  if (s_lock == NULL)
    return ESP_OK;
  if (xSemaphoreTakeRecursive(s_lock, pdMS_TO_TICKS(EXPLORER_LOCK_MS)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  esp_err_t error = ESP_OK;
  if (gatt_explorer_is_busy()) {
    error = ESP_ERR_INVALID_STATE;
  } else if (s_has_npl) {
    ble_npl_eventq_remove(nimble_port_get_dflt_eventq(), &s_command);
    ble_npl_callout_deinit(&s_lease);
    ble_npl_event_deinit(&s_command);
    s_has_npl = false;
  }
  xSemaphoreGiveRecursive(s_lock);
  return error;
}

esp_err_t gatt_explorer_get_status(spi_gatt_status_t *out) {
  if (out == NULL)
    return ESP_ERR_INVALID_ARG;
  if (s_lock == NULL) {
    memset(out, 0, sizeof(*out));
    return ESP_OK;
  }
  if (xSemaphoreTakeRecursive(s_lock, pdMS_TO_TICKS(EXPLORER_LOCK_MS)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  *out = s_status;
  xSemaphoreGiveRecursive(s_lock);
  return ESP_OK;
}

esp_err_t gatt_explorer_get_record(uint8_t index, spi_gatt_record_t *out) {
  if (out == NULL || s_lock == NULL)
    return ESP_ERR_INVALID_ARG;
  if (xSemaphoreTakeRecursive(s_lock, pdMS_TO_TICKS(EXPLORER_LOCK_MS)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  esp_err_t result = index < s_status.count ? ESP_OK : ESP_ERR_INVALID_ARG;
  if (result == ESP_OK)
    *out = s_records[index];
  xSemaphoreGiveRecursive(s_lock);
  return result;
}

esp_err_t gatt_explorer_read(uint16_t handle) {
  if (s_lock == NULL)
    return ESP_ERR_INVALID_STATE;
  if (xSemaphoreTakeRecursive(s_lock, pdMS_TO_TICKS(EXPLORER_LOCK_MS)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  bool can_read = false;
  if (s_status.state == SPI_GATT_READY) {
    for (uint8_t index = 0; index < s_status.count; ++index)
      if (s_records[index].kind == SPI_GATT_CHARACTERISTIC &&
          s_records[index].value_handle == handle &&
          (s_records[index].properties & SPI_GATT_PROPERTY_READ) != 0)
        can_read = true;
  }
  if (can_read) {
    s_read_handle = handle;
    s_status.value_length = 0;
    s_status.value_handle = handle;
    s_status.error = 0;
    s_status.state = SPI_GATT_READING;
    s_has_read = true;
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_command);
  }
  xSemaphoreGiveRecursive(s_lock);
  return can_read ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t gatt_explorer_stop(void) {
  if (s_lock == NULL)
    return ESP_OK;
  if (xSemaphoreTakeRecursive(s_lock, pdMS_TO_TICKS(EXPLORER_LOCK_MS)) != pdTRUE)
    return ESP_ERR_TIMEOUT;
  if (s_status.state != SPI_GATT_IDLE && s_status.state != SPI_GATT_ERROR) {
    s_status.state = SPI_GATT_STOPPING;
    s_has_stop = true;
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_command);
  }
  xSemaphoreGiveRecursive(s_lock);
  return ESP_OK;
}

static void on_command(struct ble_npl_event *event) {
  (void)event;
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  if (s_has_stop) {
    s_has_stop = s_has_read = false;
    if (s_has_start) {
      s_has_start = false;
      s_status.state = SPI_GATT_IDLE;
    } else {
      close_connection();
    }
  } else if (s_has_start) {
    s_has_start = false;
    int timer_error =
        ble_npl_callout_reset(&s_lease, ble_npl_time_ms_to_ticks32(EXPLORER_LEASE_MS));
    if (timer_error != 0) {
      s_status.error = BLE_HS_EOS;
      s_status.state = SPI_GATT_ERROR;
      xSemaphoreGiveRecursive(s_lock);
      return;
    }
    esp_err_t error = bluetooth_service_connect(s_address, s_address_type, on_gap);
    if (error != ESP_OK)
      fail(BLE_HS_ENOTCONN);
  } else if (s_has_read) {
    s_has_read = false;
    if (s_status.state != SPI_GATT_READING) {
      xSemaphoreGiveRecursive(s_lock);
      return;
    }
    int error =
        ble_gattc_read(s_connection, s_read_handle, on_read, (void *)(uintptr_t)s_generation);
    if (error != 0) {
      s_status.error = error;
      s_status.state = SPI_GATT_READY;
    }
  }
  xSemaphoreGiveRecursive(s_lock);
}

static void on_lease(struct ble_npl_event *event) {
  (void)event;
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  if (s_status.state != SPI_GATT_IDLE && s_status.state != SPI_GATT_ERROR) {
    if (s_status.state != SPI_GATT_STOPPING)
      s_status.error = BLE_HS_ETIMEOUT;
    close_connection();
  }
  xSemaphoreGiveRecursive(s_lock);
}

static void close_connection(void) {
  ble_npl_callout_stop(&s_lease);
  s_status.state = SPI_GATT_STOPPING;
  if (s_connection != EXPLORER_NO_CONNECTION) {
    int error = ble_gap_terminate(s_connection, EXPLORER_STOP_REASON);
    if (error == BLE_HS_ENOTCONN) {
      s_connection = EXPLORER_NO_CONNECTION;
      s_status.state = s_status.error == 0 ? SPI_GATT_IDLE : SPI_GATT_ERROR;
    }
  } else {
    int error = ble_gap_conn_cancel();
    if (error == BLE_HS_EALREADY)
      s_status.state = s_status.error == 0 ? SPI_GATT_IDLE : SPI_GATT_ERROR;
  }
  if (s_status.state == SPI_GATT_STOPPING)
    ble_npl_callout_reset(&s_lease, ble_npl_time_ms_to_ticks32(EXPLORER_CLOSE_RETRY_MS));
}

static void fail(uint16_t error) {
  s_status.error = error;
  ESP_LOGW(TAG, "Remote GATT failed: %u", (unsigned)error);
  close_connection();
}

static int on_gap(struct ble_gap_event *event, void *arg) {
  (void)arg;
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  if (event->type == BLE_GAP_EVENT_CONNECT) {
    if (event->connect.status != 0) {
      s_connection = EXPLORER_NO_CONNECTION;
      if (s_status.state != SPI_GATT_STOPPING)
        s_status.error = event->connect.status;
      s_status.state = s_status.error == 0 ? SPI_GATT_IDLE : SPI_GATT_ERROR;
      ble_npl_callout_stop(&s_lease);
    } else {
      s_connection = event->connect.conn_handle;
      if (s_status.state == SPI_GATT_STOPPING) {
        close_connection();
      } else {
        s_status.state = SPI_GATT_DISCOVERING;
        int error =
            ble_gattc_disc_all_svcs(s_connection, on_service, (void *)(uintptr_t)s_generation);
        if (error != 0)
          fail(error);
      }
    }
  } else if (event->type == BLE_GAP_EVENT_DISCONNECT &&
             event->disconnect.conn.conn_handle == s_connection) {
    s_connection = EXPLORER_NO_CONNECTION;
    if (s_status.state != SPI_GATT_STOPPING)
      s_status.error = BLE_HS_ENOTCONN;
    s_status.state = s_status.error == 0 ? SPI_GATT_IDLE : SPI_GATT_ERROR;
    ble_npl_callout_stop(&s_lease);
  }
  xSemaphoreGiveRecursive(s_lock);
  return 0;
}

static void copy_uuid(spi_gatt_record_t *out, const ble_uuid_any_t *uuid) {
  out->uuid_type = uuid->u.type;
  if (uuid->u.type == BLE_UUID_TYPE_16)
    memcpy(out->uuid, &uuid->u16.value, EXPLORER_UUID16_SIZE);
  else if (uuid->u.type == BLE_UUID_TYPE_32)
    memcpy(out->uuid, &uuid->u32.value, EXPLORER_UUID32_SIZE);
  else
    memcpy(out->uuid, uuid->u128.value, sizeof(out->uuid));
}

static int on_service(uint16_t connection,
                      const struct ble_gatt_error *error,
                      const struct ble_gatt_svc *service,
                      void *arg) {
  (void)arg;
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  if ((uintptr_t)arg != s_generation || connection != s_connection ||
      s_status.state != SPI_GATT_DISCOVERING)
    goto cleanup;
  if (error->status == BLE_HS_EDONE) {
    s_service_index = 0;
    discover_next();
  } else if (error->status != 0 || service == NULL) {
    fail(error->status != 0 ? error->status : BLE_HS_EINVAL);
  } else if (s_status.count == SPI_GATT_RECORD_MAX) {
    s_status.is_truncated = true;
  } else {
    spi_gatt_record_t *record = &s_records[s_status.count++];
    record->kind = SPI_GATT_SERVICE;
    record->start_handle = service->start_handle;
    record->end_handle = service->end_handle;
    copy_uuid(record, &service->uuid);
  }
cleanup:
  xSemaphoreGiveRecursive(s_lock);
  return 0;
}

static void discover_next(void) {
  while (s_service_index < s_status.count && s_records[s_service_index].kind != SPI_GATT_SERVICE)
    ++s_service_index;
  if (s_service_index == s_status.count) {
    s_status.state = SPI_GATT_READY;
    return;
  }
  const spi_gatt_record_t *record = &s_records[s_service_index];
  int error = ble_gattc_disc_all_chrs(s_connection,
                                      record->start_handle,
                                      record->end_handle,
                                      on_characteristic,
                                      (void *)(uintptr_t)s_generation);
  if (error != 0)
    fail(error);
}

static int on_characteristic(uint16_t connection,
                             const struct ble_gatt_error *error,
                             const struct ble_gatt_chr *characteristic,
                             void *arg) {
  (void)arg;
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  if ((uintptr_t)arg != s_generation || connection != s_connection ||
      s_status.state != SPI_GATT_DISCOVERING)
    goto cleanup;
  if (error->status == BLE_HS_EDONE) {
    ++s_service_index;
    discover_next();
  } else if (error->status != 0 || characteristic == NULL) {
    fail(error->status != 0 ? error->status : BLE_HS_EINVAL);
  } else if (s_status.count == SPI_GATT_RECORD_MAX) {
    s_status.is_truncated = true;
  } else {
    spi_gatt_record_t *record = &s_records[s_status.count++];
    record->kind = SPI_GATT_CHARACTERISTIC;
    record->start_handle = s_records[s_service_index].start_handle;
    record->end_handle = s_records[s_service_index].end_handle;
    record->value_handle = characteristic->val_handle;
    record->properties = characteristic->properties;
    copy_uuid(record, &characteristic->uuid);
  }
cleanup:
  xSemaphoreGiveRecursive(s_lock);
  return 0;
}

static int on_read(uint16_t connection,
                   const struct ble_gatt_error *error,
                   struct ble_gatt_attr *attribute,
                   void *arg) {
  (void)arg;
  xSemaphoreTakeRecursive(s_lock, portMAX_DELAY);
  if ((uintptr_t)arg == s_generation && connection == s_connection &&
      s_status.state == SPI_GATT_READING) {
    s_status.error = error->status;
    s_status.value_length = 0;
    if (error->status == 0 && attribute != NULL && attribute->om != NULL &&
        attribute->handle == s_read_handle) {
      uint16_t length = OS_MBUF_PKTLEN(attribute->om);
      if (length > sizeof(s_status.value)) {
        length = sizeof(s_status.value);
        s_status.error = BLE_HS_EMSGSIZE;
      }
      if (os_mbuf_copydata(attribute->om, 0, length, s_status.value) == 0)
        s_status.value_length = length;
      else
        s_status.error = BLE_HS_EBADDATA;
    } else if (error->status == 0) {
      s_status.error = BLE_HS_EBADDATA;
    }
    s_status.state = SPI_GATT_READY;
  }
  xSemaphoreGiveRecursive(s_lock);
  return 0;
}
