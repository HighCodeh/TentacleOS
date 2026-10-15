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

#include "ble_hid_keyboard.h"

#include <string.h>

#include "bluetooth_service.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/ans/ble_svc_ans.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE_HID_COMPOSITE";

#define HID_KEYBOARD_REPORT_SIZE 8
#define HID_MOUSE_REPORT_SIZE    4
#define HID_REPORT_ID_KEYBOARD   1
#define HID_REPORT_ID_MOUSE      2
#define KEY_DEBOUNCE_MS          15
#define HID_APPEARANCE_GENERIC   0x03C0
#define HID_ADV_NAME             "HighBoy HID"
#define HID_ADV_NAME_LEN         (sizeof(HID_ADV_NAME) - 1)

// BLE GATT UUID constants
#define UUID_DEVICE_INFO_SVC    0x180A
#define UUID_HID_SVC            0x1812
#define UUID_MFG_NAME_CHR       0x2A29
#define UUID_REPORT_MAP_CHR     0x2A4B
#define UUID_REPORT_CHR         0x2A4D
#define UUID_HID_INFO_CHR       0x2A4A
#define UUID_HID_CTRL_POINT_CHR 0x2A4C
#define UUID_PROTOCOL_MODE_CHR  0x2A4E
#define UUID_REPORT_REF_DSC     0x2908

static uint16_t s_hid_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_is_connected = false;
static bool s_services_added = false;
static bool s_advertising_requested = false;
static uint16_t s_hid_keyboard_report_handle;
static uint16_t s_hid_mouse_report_handle;
static uint8_t s_keyboard_report_ref[] = {HID_REPORT_ID_KEYBOARD, 0x01};
static uint8_t s_mouse_report_ref[] = {HID_REPORT_ID_MOUSE, 0x01};
static uint8_t s_protocol_mode = 1;

// Composite keyboard + relative mouse report map. Separate Report
// characteristics and Report Reference descriptors make both interfaces
// discoverable by macOS, Windows, Android and iOS.
static const uint8_t HID_REPORT_MAP[] = {
    0x05, 0x01,                   // Usage Page (Generic Desktop)
    0x09, 0x06,                   // Usage (Keyboard)
    0xA1, 0x01,                   // Collection (Application)
    0x85, HID_REPORT_ID_KEYBOARD, //   Report ID (keyboard)
    0x05, 0x07,                   //   Usage Page (Key Codes)
    0x19, 0xE0,                   //   Usage Minimum (224)
    0x29, 0xE7,                   //   Usage Maximum (231)
    0x15, 0x00,                   //   Logical Minimum (0)
    0x25, 0x01,                   //   Logical Maximum (1)
    0x75, 0x01,                   //   Report Size (1)
    0x95, 0x08,                   //   Report Count (8)
    0x81, 0x02,                   //   Input (Data, Variable, Absolute) ; Modifier byte
    0x95, 0x01,                   //   Report Count (1)
    0x75, 0x08,                   //   Report Size (8)
    0x81, 0x01,                   //   Input (Constant) ; Reserved byte
    0x95, 0x05,                   //   Report Count (5)
    0x75, 0x01,                   //   Report Size (1)
    0x05, 0x08,                   //   Usage Page (LEDs)
    0x19, 0x01,                   //   Usage Minimum (1)
    0x29, 0x05,                   //   Usage Maximum (5)
    0x91, 0x02,                   //   Output (Data, Variable, Absolute) ; LED report
    0x95, 0x01,                   //   Report Count (1)
    0x75, 0x03,                   //   Report Size (3)
    0x91, 0x01,                   //   Output (Constant) ; LED report padding
    0x95, 0x06,                   //   Report Count (6)
    0x75, 0x08,                   //   Report Size (8)
    0x15, 0x00,                   //   Logical Minimum (0)
    0x25, 0x65,                   //   Logical Maximum (101)
    0x05, 0x07,                   //   Usage Page (Key Codes)
    0x19, 0x00,                   //   Usage Minimum (0)
    0x29, 0x65,                   //   Usage Maximum (101)
    0x81, 0x00,                   //   Input (Data, Array) ; Key arrays (6 bytes)
    0xC0,                         // End keyboard collection

    0x05, 0x01,                // Usage Page (Generic Desktop)
    0x09, 0x02,                // Usage (Mouse)
    0xA1, 0x01,                // Collection (Application)
    0x85, HID_REPORT_ID_MOUSE, //   Report ID (mouse)
    0x09, 0x01,                //   Usage (Pointer)
    0xA1, 0x00,                //   Collection (Physical)
    0x05, 0x09,                //     Usage Page (Buttons)
    0x19, 0x01,                //     Usage Minimum (Button 1)
    0x29, 0x03,                //     Usage Maximum (Button 3)
    0x15, 0x00,                //     Logical Minimum (0)
    0x25, 0x01,                //     Logical Maximum (1)
    0x95, 0x03,                //     Report Count (3)
    0x75, 0x01,                //     Report Size (1)
    0x81, 0x02,                //     Input (Data, Variable, Absolute)
    0x95, 0x01,                //     Report Count (1)
    0x75, 0x05,                //     Report Size (5)
    0x81, 0x01,                //     Input (Constant)
    0x05, 0x01,                //     Usage Page (Generic Desktop)
    0x09, 0x30,                //     Usage X
    0x09, 0x31,                //     Usage Y
    0x09, 0x38,                //     Usage Wheel
    0x15, 0x81,                //     Logical Minimum (-127)
    0x25, 0x7F,                //     Logical Maximum (127)
    0x75, 0x08,                //     Report Size (8)
    0x95, 0x03,                //     Report Count (3)
    0x81, 0x06,                //     Input (Data, Variable, Relative)
    0xC0,                      //   End physical collection
    0xC0                       // End mouse collection
};

static int hid_gap_event(struct ble_gap_event *event, void *arg);
static esp_err_t hid_start_advertising(void);

static int hid_access_cb(uint16_t conn_handle,
                         uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt,
                         void *arg) {
  uint16_t uuid16 = ble_uuid_u16(ctxt->chr->uuid);

  (void)conn_handle;

  if (uuid16 == UUID_MFG_NAME_CHR) {
    static const char manufacturer[] = "High Boy";
    int rc = os_mbuf_append(ctxt->om, manufacturer, sizeof(manufacturer) - 1);
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }

  // Report Map
  if (uuid16 == UUID_REPORT_MAP_CHR) {
    int rc = os_mbuf_append(ctxt->om, HID_REPORT_MAP, sizeof(HID_REPORT_MAP));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }

  // HID Information: bcdHID(2), bCountryCode(1), Flags(1)
  if (uuid16 == UUID_HID_INFO_CHR) {
    // HID v1.11, Country 0, RemoteWake+NormallyConnectable
    uint8_t info[] = {0x11, 0x01, 0x00, 0x02};
    int rc = os_mbuf_append(ctxt->om, info, sizeof(info));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }

  // Protocol Mode
  if (uuid16 == UUID_PROTOCOL_MODE_CHR) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
      os_mbuf_append(ctxt->om, &s_protocol_mode, 1);
      return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && OS_MBUF_PKTLEN(ctxt->om) == 1) {
      uint8_t requested = 1;
      os_mbuf_copydata(ctxt->om, 0, 1, &requested);
      // This device implements Report Protocol. Keep the published state
      // truthful even if a central requests the optional Boot Protocol.
      s_protocol_mode = requested == 1 ? 1 : 1;
      return 0;
    }
  }

  if (uuid16 == UUID_REPORT_CHR && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    if (attr_handle == s_hid_mouse_report_handle) {
      const uint8_t report[HID_MOUSE_REPORT_SIZE] = {0};
      int rc = os_mbuf_append(ctxt->om, report, sizeof(report));
      return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    const uint8_t report[HID_KEYBOARD_REPORT_SIZE] = {0};
    int rc = os_mbuf_append(ctxt->om, report, sizeof(report));
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }

  if (uuid16 == UUID_HID_CTRL_POINT_CHR) {
    // Suspend / exit-suspend is advisory for this low-power peripheral.
    return 0;
  }

  return 0;
}

static int hid_report_ref_access_cb(uint16_t conn_handle,
                                    uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt,
                                    void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  const uint8_t *report_ref = (const uint8_t *)arg;
  int rc = os_mbuf_append(ctxt->om, report_ref, 2);
  return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_svc_def s_hid_svcs[] = {
    {
        // Service: Device Information
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(UUID_DEVICE_INFO_SVC),
        .characteristics =
            (struct ble_gatt_chr_def[]){{
                                            .uuid = BLE_UUID16_DECLARE(UUID_MFG_NAME_CHR),
                                            .access_cb = hid_access_cb,
                                            .flags = BLE_GATT_CHR_F_READ,
                                        },
                                        {
                                            0,
                                        }},
    },
    {
        // Service: Human Interface Device
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(UUID_HID_SVC),
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    // Report Map
                    .uuid = BLE_UUID16_DECLARE(UUID_REPORT_MAP_CHR),
                    .access_cb = hid_access_cb,
                    .flags = BLE_GATT_CHR_F_READ,
                },
                {
                    // Keyboard input report
                    .uuid = BLE_UUID16_DECLARE(UUID_REPORT_CHR),
                    .access_cb = hid_access_cb,
                    .descriptors =
                        (struct ble_gatt_dsc_def[]){
                            {
                                .uuid = BLE_UUID16_DECLARE(UUID_REPORT_REF_DSC),
                                .att_flags = BLE_ATT_F_READ,
                                .access_cb = hid_report_ref_access_cb,
                                .arg = s_keyboard_report_ref,
                            },
                            {0}},
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &s_hid_keyboard_report_handle,
                },
                {
                    // Mouse input report
                    .uuid = BLE_UUID16_DECLARE(UUID_REPORT_CHR),
                    .access_cb = hid_access_cb,
                    .descriptors =
                        (struct ble_gatt_dsc_def[]){
                            {
                                .uuid = BLE_UUID16_DECLARE(UUID_REPORT_REF_DSC),
                                .att_flags = BLE_ATT_F_READ,
                                .access_cb = hid_report_ref_access_cb,
                                .arg = s_mouse_report_ref,
                            },
                            {0}},
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                    .val_handle = &s_hid_mouse_report_handle,
                },
                {
                    // HID Information
                    .uuid = BLE_UUID16_DECLARE(UUID_HID_INFO_CHR),
                    .access_cb = hid_access_cb,
                    .flags = BLE_GATT_CHR_F_READ,
                },
                {
                    // Protocol Mode
                    .uuid = BLE_UUID16_DECLARE(UUID_PROTOCOL_MODE_CHR),
                    .access_cb = hid_access_cb,
                    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
                },
                {
                    // HID Control Point
                    .uuid = BLE_UUID16_DECLARE(UUID_HID_CTRL_POINT_CHR),
                    .access_cb = hid_access_cb,
                    .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
                },
                {
                    0,
                }},
    },
    {
        0,
    }};

esp_err_t ble_hid_register_services(void) {
  if (!bluetooth_service_is_initialized() || bluetooth_service_is_running()) {
    ESP_LOGE(TAG, "HID services must be registered before the host starts");
    return ESP_ERR_INVALID_STATE;
  }

  if (s_services_added)
    return ESP_OK;

  int rc = ble_gatts_count_cfg(s_hid_svcs);
  if (rc != 0) {
    ESP_LOGE(TAG, "GATTS count cfg failed: rc=%d", rc);
    return ESP_FAIL;
  }

  rc = ble_gatts_add_svcs(s_hid_svcs);
  if (rc != 0) {
    ESP_LOGE(TAG, "GATTS add svcs failed: rc=%d", rc);
    return ESP_FAIL;
  }
  s_services_added = true;
  ESP_LOGI(TAG, "composite keyboard/mouse service registered");
  return ESP_OK;
}

bool ble_hid_services_registered(void) {
  return s_services_added;
}

void ble_hid_stack_reset(void) {
  s_hid_conn_handle = BLE_HS_CONN_HANDLE_NONE;
  s_is_connected = false;
  s_services_added = false;
  s_advertising_requested = false;
  s_hid_keyboard_report_handle = 0;
  s_hid_mouse_report_handle = 0;
  s_protocol_mode = 1;
}

esp_err_t ble_hid_init(void) {
  if (!bluetooth_service_is_initialized() || !bluetooth_service_is_running() || !s_services_added) {
    ESP_LOGE(TAG, "NimBLE HID host is not ready");
    return ESP_ERR_INVALID_STATE;
  }

  s_advertising_requested = true;
  return hid_start_advertising();
}

static esp_err_t hid_start_advertising(void) {
  if (!s_advertising_requested || s_is_connected) {
    return ESP_OK;
  }

  if (ble_gap_adv_active()) {
    int stop_rc = ble_gap_adv_stop();
    if (stop_rc != 0) {
      ESP_LOGE(TAG, "Failed to stop previous advertisement: rc=%d", stop_rc);
      return ESP_FAIL;
    }
  }

  struct ble_gap_adv_params adv_params = {0};
  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  struct ble_hs_adv_fields fields = {0};
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.appearance = HID_APPEARANCE_GENERIC;
  fields.name = (uint8_t *)HID_ADV_NAME;
  fields.name_len = HID_ADV_NAME_LEN;
  fields.name_is_complete = 1;
  fields.uuids16 = (ble_uuid16_t[]){BLE_UUID16_INIT(UUID_HID_SVC)};
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;

  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "Failed to set HID advertisement fields: rc=%d", rc);
    return ESP_FAIL;
  }

  uint8_t own_addr_type = bluetooth_service_get_own_addr_type();
  rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, hid_gap_event, NULL);
  if (rc != 0) {
    s_advertising_requested = false;
    ESP_LOGE(TAG, "Failed to start HID advertising: rc=%d addr_type=%u", rc, own_addr_type);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "HID advertising as %s (addr_type=%u)", HID_ADV_NAME, own_addr_type);
  return ESP_OK;
}

esp_err_t ble_hid_deinit(void) {
  s_advertising_requested = false;
  if (s_is_connected) {
    ble_gap_terminate(s_hid_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
  }
  if (ble_gap_adv_active()) {
    int rc = ble_gap_adv_stop();
    if (rc != 0) {
      ESP_LOGW(TAG, "Failed to stop HID advertising: rc=%d", rc);
    }
  }
  // NimBLE does not easily support removing services at runtime without reset.
  // We just stop advertising.
  return ESP_OK;
}

bool ble_hid_is_connected(void) {
  return s_is_connected;
}

void ble_hid_send_key(uint8_t keycode, uint8_t modifier) {
  if (!s_is_connected)
    return;

  // The Report Reference descriptor identifies report ID 1, so HOGP sends the
  // characteristic value without an in-band report-ID byte.
  uint8_t report[HID_KEYBOARD_REPORT_SIZE] = {0};
  report[0] = modifier;
  report[2] = keycode;

  struct os_mbuf *om = ble_hs_mbuf_from_flat(report, sizeof(report));
  ble_gatts_notify_custom(s_hid_conn_handle, s_hid_keyboard_report_handle, om);

  vTaskDelay(pdMS_TO_TICKS(KEY_DEBOUNCE_MS));

  // Release
  memset(report, 0, sizeof(report));
  om = ble_hs_mbuf_from_flat(report, sizeof(report));
  ble_gatts_notify_custom(s_hid_conn_handle, s_hid_keyboard_report_handle, om);

  vTaskDelay(pdMS_TO_TICKS(KEY_DEBOUNCE_MS));
}

void ble_hid_send_mouse(int8_t x, int8_t y, uint8_t buttons, int8_t wheel) {
  if (!s_is_connected)
    return;

  uint8_t report[HID_MOUSE_REPORT_SIZE] = {
      (uint8_t)(buttons & 0x07),
      (uint8_t)x,
      (uint8_t)y,
      (uint8_t)wheel,
  };
  struct os_mbuf *om = ble_hs_mbuf_from_flat(report, sizeof(report));
  int rc = ble_gatts_notify_custom(s_hid_conn_handle, s_hid_mouse_report_handle, om);
  if (rc != 0)
    ESP_LOGW(TAG, "mouse notify failed: rc=%d", rc);
}

static int hid_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        s_hid_conn_handle = event->connect.conn_handle;
        s_is_connected = true;
        ESP_LOGI(TAG, "HID Connected");
      } else {
        ESP_LOGW(TAG, "HID connection failed: status=%d", event->connect.status);
        hid_start_advertising();
      }
      break;
    case BLE_GAP_EVENT_DISCONNECT:
      s_hid_conn_handle = BLE_HS_CONN_HANDLE_NONE;
      s_is_connected = false;
      ESP_LOGI(TAG, "HID Disconnected");
      // Auto re-advertise
      hid_start_advertising();
      break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
      ESP_LOGI(TAG, "HID advertising complete: reason=%d", event->adv_complete.reason);
      hid_start_advertising();
      break;
  }
  return 0;
}
