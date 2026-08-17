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

#include "meshtastic_gatt.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "bluetooth_service.h"
#include "meshtastic_transport.h"

extern void ble_store_config_init(void);

static const char *TAG = "MESH_GATT";

#define GATT_PREFERRED_MTU          512
#define GATT_PASSKEY_MIN            100000
#define GATT_PASSKEY_RANGE          900000
#define GATT_FROMRADIO_QUEUE_SLOTS  16
#define GATT_FROMRADIO_FRAME_MAX    512
#define GATT_BATTERY_STUB_PCT       87
#define GATT_DEVICE_NAME_LEN        32
#define GATT_QUEUE_MUTEX_TIMEOUT_MS 50

static const ble_uuid128_t MESH_SERVICE_UUID = BLE_UUID128_INIT(
    0xfd, 0xea, 0x73, 0xe2, 0xca, 0x5d, 0xa8, 0x9f, 0x1f, 0x46, 0xa8, 0x15, 0x18, 0xb2, 0xa1, 0x6b);
static const ble_uuid128_t FROMRADIO_UUID = BLE_UUID128_INIT(
    0x02, 0x00, 0x12, 0xac, 0x42, 0x02, 0x78, 0xb8, 0xed, 0x11, 0x93, 0x49, 0x9e, 0xe6, 0x55, 0x2c);
static const ble_uuid128_t TORADIO_UUID = BLE_UUID128_INIT(
    0xe7, 0x01, 0x44, 0x12, 0x66, 0x78, 0xdd, 0xa1, 0xad, 0x4d, 0x9e, 0x12, 0xd2, 0x76, 0x5c, 0xf7);
static const ble_uuid128_t FROMNUM_UUID = BLE_UUID128_INIT(
    0x53, 0x44, 0xe3, 0x47, 0x75, 0xaa, 0x70, 0xa6, 0x66, 0x4f, 0x00, 0xa8, 0x8c, 0xa1, 0x9d, 0xed);
static const ble_uuid128_t LOGRADIO_UUID = BLE_UUID128_INIT(
    0x47, 0x95, 0xdf, 0x8c, 0xde, 0xe9, 0x44, 0x99, 0x23, 0x44, 0xe6, 0x06, 0x49, 0x6e, 0x3d, 0x5a);

static const ble_uuid16_t BATTERY_SERVICE_UUID = BLE_UUID16_INIT(0x180F);
static const ble_uuid16_t BATTERY_LEVEL_UUID = BLE_UUID16_INIT(0x2A19);

typedef struct {
  bool is_used;
  uint16_t len;
  uint8_t buf[GATT_FROMRADIO_FRAME_MAX];
} gatt_fromradio_slot_t;

static bool s_is_running = false;
static bool s_is_connected = false;
static bool s_fromnum_subscribed = false;
static bool s_logradio_subscribed = false;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_fromnum_handle = 0;
static uint16_t s_logradio_handle = 0;
static uint8_t s_own_addr_type = 0;
static uint32_t s_random_passkey = 0;
static uint8_t s_battery_level = GATT_BATTERY_STUB_PCT;
static uint32_t s_node_num = 0;
static uint8_t s_toradio_buf[GATT_FROMRADIO_FRAME_MAX];
static gatt_fromradio_slot_t s_queue[GATT_FROMRADIO_QUEUE_SLOTS];
static uint8_t s_queue_head = 0;
static uint8_t s_queue_tail = 0;
static SemaphoreHandle_t s_queue_mutex = NULL;

static void advertise_start(void);
static int gap_event(struct ble_gap_event *event, void *arg);
static int
on_toradio_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int
on_fromradio_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int
on_fromnum_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int
on_logradio_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int
on_battery_level_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg);
static void on_sync(void);
static void on_reset(int reason);
static void host_task(void *param);
static bool queue_pop_locked(uint8_t *out_buf, uint16_t *out_len);
static void queue_push_locked(const uint8_t *frame, uint16_t len);
static void notify_fromnum(uint32_t counter);

static const struct ble_gatt_svc_def GATT_SERVICES[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &MESH_SERVICE_UUID.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &TORADIO_UUID.u,
                    .access_cb = on_toradio_access,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                },
                {
                    .uuid = &FROMRADIO_UUID.u,
                    .access_cb = on_fromradio_access,
                    .flags = BLE_GATT_CHR_F_READ,
                },
                {
                    .uuid = &FROMNUM_UUID.u,
                    .access_cb = on_fromnum_access,
                    .val_handle = &s_fromnum_handle,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                },
                {
                    .uuid = &LOGRADIO_UUID.u,
                    .access_cb = on_logradio_access,
                    .val_handle = &s_logradio_handle,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                },
                {0},
            },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &BATTERY_SERVICE_UUID.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &BATTERY_LEVEL_UUID.u,
                    .access_cb = on_battery_level_access,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                },
                {0},
            },
    },
    {0},
};

esp_err_t meshtastic_gatt_init(uint32_t node_num) {
  if (s_is_running) {
    return ESP_ERR_INVALID_STATE;
  }
  if (bluetooth_service_is_running()) {
    ESP_LOGE(TAG, "bluetooth_service already owns NimBLE — refuse init");
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = meshtastic_transport_init();
  if (ret != ESP_OK) {
    return ret;
  }

  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    ret = nvs_flash_init();
  }
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  s_node_num = node_num;
  s_is_connected = false;
  s_fromnum_subscribed = false;
  s_logradio_subscribed = false;
  s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
  memset(s_queue, 0, sizeof(s_queue));
  s_queue_head = 0;
  s_queue_tail = 0;

  if (s_queue_mutex == NULL) {
    s_queue_mutex = xSemaphoreCreateMutex();
    if (s_queue_mutex == NULL) {
      ESP_LOGE(TAG, "Failed to create queue mutex");
      return ESP_ERR_NO_MEM;
    }
  }

  ret = nimble_port_init();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
  ble_hs_cfg.sm_bonding = 1;
  ble_hs_cfg.sm_mitm = 1;
  ble_hs_cfg.sm_sc = 1;
  ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  char name[GATT_DEVICE_NAME_LEN];
  snprintf(name, sizeof(name), "Highboy_%04lx", (unsigned long)(node_num & 0xFFFF));
  ble_svc_gap_device_name_set(name);

  ble_svc_gap_init();
  ble_svc_gatt_init();

  int rc = ble_gatts_count_cfg(GATT_SERVICES);
  if (rc != 0) {
    ESP_LOGE(TAG, "ble_gatts_count_cfg failed rc=%d", rc);
    return ESP_FAIL;
  }
  rc = ble_gatts_add_svcs(GATT_SERVICES);
  if (rc != 0) {
    ESP_LOGE(TAG, "ble_gatts_add_svcs failed rc=%d", rc);
    return ESP_FAIL;
  }

  ble_store_config_init();
  nimble_port_freertos_init(host_task);

  s_is_running = true;
  ESP_LOGI(TAG, "Initialized — name='%s'", name);
  return ESP_OK;
}

void meshtastic_gatt_stop(void) {
  if (!s_is_running) {
    return;
  }
  ble_gap_adv_stop();
  if (s_is_connected && s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
    ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
  }
  if (nimble_port_stop() == 0) {
    nimble_port_deinit();
  }
  s_is_running = false;
  s_is_connected = false;
  s_fromnum_subscribed = false;
  s_logradio_subscribed = false;
  s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
  meshtastic_transport_reset();
}

bool meshtastic_gatt_is_running(void) {
  return s_is_running;
}

bool meshtastic_gatt_is_connected(void) {
  return s_is_connected;
}

bool meshtastic_gatt_is_fromnum_subscribed(void) {
  return s_fromnum_subscribed;
}

bool meshtastic_gatt_is_logradio_subscribed(void) {
  return s_logradio_subscribed;
}

void meshtastic_gatt_enqueue_fromradio(const uint8_t *frame, uint16_t len) {
  if (frame == NULL || len == 0 || len > GATT_FROMRADIO_FRAME_MAX) {
    return;
  }
  if (s_queue_mutex == NULL) {
    return;
  }
  if (xSemaphoreTake(s_queue_mutex, pdMS_TO_TICKS(GATT_QUEUE_MUTEX_TIMEOUT_MS)) != pdTRUE) {
    return;
  }
  queue_push_locked(frame, len);
  xSemaphoreGive(s_queue_mutex);

  if (s_is_connected && s_fromnum_subscribed) {
    notify_fromnum(meshtastic_transport_bump_fromnum());
  }
}

void meshtastic_gatt_notify_log(const uint8_t *chunk, uint16_t len) {
  if (chunk == NULL || len == 0) {
    return;
  }
  if (!s_is_connected || !s_logradio_subscribed || s_logradio_handle == 0) {
    return;
  }
  struct os_mbuf *om = ble_hs_mbuf_from_flat(chunk, len);
  if (om != NULL) {
    ble_gatts_notify_custom(s_conn_handle, s_logradio_handle, om);
  }
}

static int
on_toradio_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn;
  (void)attr;
  (void)arg;
  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
  if (len == 0 || len > sizeof(s_toradio_buf)) {
    return 0;
  }
  os_mbuf_copydata(ctxt->om, 0, len, s_toradio_buf);
  meshtastic_transport_send_toradio(s_toradio_buf, len);
  return 0;
}

static int
on_fromradio_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn;
  (void)attr;
  (void)arg;
  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  if (s_queue_mutex == NULL) {
    return 0;
  }
  if (xSemaphoreTake(s_queue_mutex, pdMS_TO_TICKS(GATT_QUEUE_MUTEX_TIMEOUT_MS)) != pdTRUE) {
    return 0;
  }
  uint8_t out[GATT_FROMRADIO_FRAME_MAX];
  uint16_t out_len = 0;
  if (queue_pop_locked(out, &out_len)) {
    os_mbuf_append(ctxt->om, out, out_len);
  }
  xSemaphoreGive(s_queue_mutex);
  return 0;
}

static int
on_fromnum_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn;
  (void)attr;
  (void)arg;
  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    spi_mesh_status_t status;
    meshtastic_transport_get_status(&status);
    os_mbuf_append(ctxt->om, &status.fromnum_counter, sizeof(status.fromnum_counter));
  }
  return 0;
}

static int
on_logradio_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn;
  (void)attr;
  (void)arg;
  (void)ctxt;
  return 0;
}

static int on_battery_level_access(uint16_t conn,
                                   uint16_t attr,
                                   struct ble_gatt_access_ctxt *ctxt,
                                   void *arg) {
  (void)conn;
  (void)attr;
  (void)arg;
  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    os_mbuf_append(ctxt->om, &s_battery_level, 1);
  }
  return 0;
}

static int gap_event(struct ble_gap_event *event, void *arg) {
  (void)arg;
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        s_conn_handle = event->connect.conn_handle;
        s_is_connected = true;
        s_fromnum_subscribed = false;
        s_logradio_subscribed = false;
        ESP_LOGI(TAG, "Client connected handle=%u", s_conn_handle);
        ble_att_set_preferred_mtu(GATT_PREFERRED_MTU);
      } else {
        ESP_LOGW(TAG, "Connection failed status=%d", event->connect.status);
        advertise_start();
      }
      break;

    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(TAG, "Client disconnected reason=0x%x", event->disconnect.reason);
      s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
      s_is_connected = false;
      s_fromnum_subscribed = false;
      s_logradio_subscribed = false;
      meshtastic_transport_reset();
      advertise_start();
      break;

    case BLE_GAP_EVENT_MTU:
      ESP_LOGI(TAG, "MTU updated to %u", event->mtu.value);
      break;

    case BLE_GAP_EVENT_SUBSCRIBE:
      if (event->subscribe.attr_handle == s_fromnum_handle) {
        s_fromnum_subscribed = (event->subscribe.cur_notify != 0);
      }
      if (event->subscribe.attr_handle == s_logradio_handle) {
        s_logradio_subscribed = (event->subscribe.cur_notify != 0);
      }
      break;

    case BLE_GAP_EVENT_ENC_CHANGE:
      ESP_LOGI(TAG, "Encryption status=%d", event->enc_change.status);
      break;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
      if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
        s_random_passkey = GATT_PASSKEY_MIN + (esp_random() % GATT_PASSKEY_RANGE);
        struct ble_sm_io pk = {
            .action = BLE_SM_IOACT_DISP,
            .passkey = s_random_passkey,
        };
        ble_sm_inject_io(event->passkey.conn_handle, &pk);
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "  BLE pairing PIN: %lu", (unsigned long)s_random_passkey);
        ESP_LOGI(TAG, "========================================");
      }
      break;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
      struct ble_gap_conn_desc desc;
      if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) != 0) {
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
      }
      ble_store_util_delete_peer(&desc.peer_id_addr);
      return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    default:
      break;
  }
  return 0;
}

static void advertise_start(void) {
  struct ble_gap_adv_params adv = {
      .conn_mode = BLE_GAP_CONN_MODE_UND,
      .disc_mode = BLE_GAP_DISC_MODE_GEN,
  };

  struct ble_hs_adv_fields fields = {0};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.uuids128 = (ble_uuid128_t *)&MESH_SERVICE_UUID;
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;
  ble_gap_adv_set_fields(&fields);

  struct ble_hs_adv_fields rsp = {0};
  rsp.name = (uint8_t *)ble_svc_gap_device_name();
  rsp.name_len = strlen(ble_svc_gap_device_name());
  rsp.name_is_complete = 1;
  ble_gap_adv_rsp_set_fields(&rsp);

  int rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv, gap_event, NULL);
  if (rc != 0 && rc != BLE_HS_EALREADY) {
    ESP_LOGE(TAG, "adv_start failed rc=%d", rc);
    return;
  }
  ESP_LOGI(TAG, "Advertising as '%s'", ble_svc_gap_device_name());
}

static void on_sync(void) {
  ble_hs_util_ensure_addr(0);
  ble_hs_id_infer_auto(0, &s_own_addr_type);

  uint8_t mac[6];
  ble_hs_id_copy_addr(s_own_addr_type, mac, NULL);
  ESP_LOGI(TAG,
           "BLE addr %02x:%02x:%02x:%02x:%02x:%02x type=%u",
           mac[5],
           mac[4],
           mac[3],
           mac[2],
           mac[1],
           mac[0],
           s_own_addr_type);

  advertise_start();
}

static void on_reset(int reason) {
  ESP_LOGW(TAG, "NimBLE reset reason=%d", reason);
}

static void host_task(void *param) {
  (void)param;
  ESP_LOGI(TAG, "NimBLE host task running");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

static bool queue_pop_locked(uint8_t *out_buf, uint16_t *out_len) {
  gatt_fromradio_slot_t *slot = &s_queue[s_queue_head];
  if (!slot->is_used) {
    if (out_len != NULL) {
      *out_len = 0;
    }
    return false;
  }
  if (out_len != NULL) {
    *out_len = slot->len;
  }
  if (out_buf != NULL && slot->len > 0) {
    memcpy(out_buf, slot->buf, slot->len);
  }
  slot->is_used = false;
  s_queue_head = (uint8_t)((s_queue_head + 1) % GATT_FROMRADIO_QUEUE_SLOTS);
  return true;
}

static void queue_push_locked(const uint8_t *frame, uint16_t len) {
  gatt_fromradio_slot_t *slot = &s_queue[s_queue_tail];
  if (slot->is_used) {
    s_queue_head = (uint8_t)((s_queue_head + 1) % GATT_FROMRADIO_QUEUE_SLOTS);
  }
  slot->len = len;
  memcpy(slot->buf, frame, len);
  slot->is_used = true;
  s_queue_tail = (uint8_t)((s_queue_tail + 1) % GATT_FROMRADIO_QUEUE_SLOTS);
}

static void notify_fromnum(uint32_t counter) {
  if (s_fromnum_handle == 0 || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    return;
  }
  struct os_mbuf *om = ble_hs_mbuf_from_flat(&counter, sizeof(counter));
  if (om != NULL) {
    ble_gatts_notify_custom(s_conn_handle, s_fromnum_handle, om);
  }
}
