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

#include "meshcore_gatt.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
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
#include "meshcore_transport.h"

extern void ble_store_config_init(void);

static const char *TAG = "MC_GATT";

#define MCORE_PREFERRED_MTU   512
#define MCORE_RX_FRAME_MAX    256
#define MCORE_DEVICE_NAME_LEN 32

static const ble_uuid128_t NUS_SERVICE_UUID = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E);
static const ble_uuid128_t NUS_RX_UUID = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E);
static const ble_uuid128_t NUS_TX_UUID = BLE_UUID128_INIT(
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x03, 0x00, 0x40, 0x6E);

static bool s_is_running = false;
static bool s_is_connected = false;
static bool s_is_subscribed = false;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_tx_attr_handle = 0;
static uint8_t s_own_addr_type = 0;
static uint32_t s_passkey = 0;
static char s_device_name[MCORE_DEVICE_NAME_LEN] = {0};
static uint8_t s_rx_buf[MCORE_RX_FRAME_MAX];

static void advertise_start(void);
static int gap_event(struct ble_gap_event *event, void *arg);
static int rx_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int tx_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg);
static void on_sync(void);
static void on_reset(int reason);
static void host_task(void *param);

static const struct ble_gatt_svc_def GATT_SERVICES[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &NUS_SERVICE_UUID.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &NUS_RX_UUID.u,
                    .access_cb = rx_access,
                    .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP |
                             BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN,
                },
                {
                    .uuid = &NUS_TX_UUID.u,
                    .access_cb = tx_access,
                    .val_handle = &s_tx_attr_handle,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC |
                             BLE_GATT_CHR_F_READ_AUTHEN,
                },
                {0},
            },
    },
    {0},
};

esp_err_t meshcore_gatt_init(const char *name_prefix, uint32_t pin) {
  if (name_prefix == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (s_is_running) {
    return ESP_ERR_INVALID_STATE;
  }
  if (bluetooth_service_is_running()) {
    ESP_LOGE(TAG, "bluetooth_service already owns NimBLE — refuse init");
    return ESP_ERR_INVALID_STATE;
  }

  esp_err_t ret = meshcore_transport_init();
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

  s_passkey = pin;
  s_is_connected = false;
  s_is_subscribed = false;
  s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

  uint8_t mac[6] = {0};
  esp_efuse_mac_get_default(mac);
  snprintf(s_device_name, sizeof(s_device_name), "%s-%02X%02X", name_prefix, mac[4], mac[5]);

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

  ble_svc_gap_init();
  ble_svc_gatt_init();
  ble_svc_gap_device_name_set(s_device_name);

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

  ble_att_set_preferred_mtu(MCORE_PREFERRED_MTU);
  ble_store_config_init();
  nimble_port_freertos_init(host_task);

  s_is_running = true;
  ESP_LOGI(TAG, "Initialized — name='%s' PIN=%lu", s_device_name, (unsigned long)pin);
  return ESP_OK;
}

void meshcore_gatt_stop(void) {
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
  s_is_subscribed = false;
  s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
  meshcore_transport_reset();
}

bool meshcore_gatt_is_running(void) {
  return s_is_running;
}

bool meshcore_gatt_is_connected(void) {
  return s_is_connected;
}

bool meshcore_gatt_is_subscribed(void) {
  return s_is_subscribed;
}

void meshcore_gatt_notify(const uint8_t *frame, uint16_t len) {
  if (frame == NULL || len == 0) {
    return;
  }
  if (!s_is_connected || s_tx_attr_handle == 0) {
    return;
  }
  struct os_mbuf *om = ble_hs_mbuf_from_flat(frame, len);
  if (om == NULL) {
    ESP_LOGW(TAG, "mbuf alloc failed (%u bytes)", len);
    return;
  }
  int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_attr_handle, om);
  if (rc != 0) {
    ESP_LOGW(TAG, "notify failed rc=%d", rc);
  }
}

static int rx_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn;
  (void)attr;
  (void)arg;
  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
  if (len == 0 || len > sizeof(s_rx_buf)) {
    return 0;
  }
  os_mbuf_copydata(ctxt->om, 0, len, s_rx_buf);
  meshcore_transport_send_to_p4(s_rx_buf, len);
  return 0;
}

static int tx_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn;
  (void)attr;
  (void)ctxt;
  (void)arg;
  return 0;
}

static int gap_event(struct ble_gap_event *event, void *arg) {
  (void)arg;
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        s_conn_handle = event->connect.conn_handle;
        s_is_connected = true;
        s_is_subscribed = false;
        ESP_LOGI(TAG, "Client connected handle=%u", s_conn_handle);
        ble_gattc_exchange_mtu(s_conn_handle, NULL, NULL);
      } else {
        ESP_LOGW(TAG, "Connect failed status=%d", event->connect.status);
        advertise_start();
      }
      break;

    case BLE_GAP_EVENT_DISCONNECT:
      ESP_LOGI(TAG, "Client disconnected reason=0x%x", event->disconnect.reason);
      s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
      s_is_connected = false;
      s_is_subscribed = false;
      meshcore_transport_reset();
      advertise_start();
      break;

    case BLE_GAP_EVENT_MTU:
      ESP_LOGI(TAG, "MTU updated to %u", event->mtu.value);
      break;

    case BLE_GAP_EVENT_SUBSCRIBE:
      if (event->subscribe.attr_handle == s_tx_attr_handle) {
        s_is_subscribed = (event->subscribe.cur_notify != 0);
        ESP_LOGI(TAG, "TX subscribe=%d", s_is_subscribed);
      }
      break;

    case BLE_GAP_EVENT_ENC_CHANGE:
      ESP_LOGI(TAG, "Encryption status=%d", event->enc_change.status);
      break;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
      if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
        struct ble_sm_io pk = {
            .action = BLE_SM_IOACT_DISP,
            .passkey = s_passkey,
        };
        ble_sm_inject_io(event->passkey.conn_handle, &pk);
        ESP_LOGI(TAG, "Displaying passkey: %lu", (unsigned long)s_passkey);
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
  fields.tx_pwr_lvl_is_present = 1;
  fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
  fields.name = (uint8_t *)s_device_name;
  fields.name_len = strlen(s_device_name);
  fields.name_is_complete = 1;
  ble_gap_adv_set_fields(&fields);

  struct ble_hs_adv_fields rsp = {0};
  rsp.uuids128 = (ble_uuid128_t *)&NUS_SERVICE_UUID;
  rsp.num_uuids128 = 1;
  rsp.uuids128_is_complete = 1;
  ble_gap_adv_rsp_set_fields(&rsp);

  int rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv, gap_event, NULL);
  if (rc != 0 && rc != BLE_HS_EALREADY) {
    ESP_LOGE(TAG, "adv_start failed rc=%d", rc);
    return;
  }
  ESP_LOGI(TAG, "Advertising '%s' (PIN %lu)", s_device_name, (unsigned long)s_passkey);
}

static void on_sync(void) {
  ble_hs_util_ensure_addr(0);
  ble_hs_id_infer_auto(0, &s_own_addr_type);
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
