// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ble_hid_keyboard.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "services/ans/ble_svc_ans.h"
#include <string.h>

static const char *TAG = "BLE_HID";

static uint16_t hid_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool is_connected = false;
static uint16_t hid_report_handle;

// HID Report Map para Teclado Padrão
static const uint8_t hid_report_map[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x05, 0x07,       //   Usage Page (Key Codes)
    0x19, 0xE0,       //   Usage Minimum (224)
    0x29, 0xE7,       //   Usage Maximum (231)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)
    0x81, 0x02,       //   Input (Data, Variable, Absolute) ; Modifier byte
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x01,       //   Input (Constant) ; Reserved byte
    0x95, 0x05,       //   Report Count (5)
    0x75, 0x01,       //   Report Size (1)
    0x05, 0x08,       //   Usage Page (LEDs)
    0x19, 0x01,       //   Usage Minimum (1)
    0x29, 0x05,       //   Usage Maximum (5)
    0x91, 0x02,       //   Output (Data, Variable, Absolute) ; LED report
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x03,       //   Report Size (3)
    0x91, 0x01,       //   Output (Constant) ; LED report padding
    0x95, 0x06,       //   Report Count (6)
    0x75, 0x08,       //   Report Size (8)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x65,       //   Logical Maximum (101)
    0x05, 0x07,       //   Usage Page (Key Codes)
    0x19, 0x00,       //   Usage Minimum (0)
    0x29, 0x65,       //   Usage Maximum (101)
    0x81, 0x00,       //   Input (Data, Array) ; Key arrays (6 bytes)
    0xC0              // End Collection
};

static int hid_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                         struct ble_gatt_access_ctxt *ctxt, void *arg) {
    uint16_t uuid16 = ble_uuid_u16(ctxt->chr->uuid);

    // Report Map (0x2A4B)
    if (uuid16 == 0x2A4B) {
        int rc = os_mbuf_append(ctxt->om, hid_report_map, sizeof(hid_report_map));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    
    // HID Information (0x2A4A)
    if (uuid16 == 0x2A4A) {
        // bcdHID(2), bCountryCode(1), Flags(1)
        uint8_t info[] = {0x11, 0x01, 0x00, 0x02}; // HID v1.11, Country 0, RemoteWake+NormallyConnectable
        int rc = os_mbuf_append(ctxt->om, info, sizeof(info));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    // Protocol Mode (0x2A4E)
    if (uuid16 == 0x2A4E) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            uint8_t mode = 1; // Report Protocol
            os_mbuf_append(ctxt->om, &mode, 1);
            return 0;
        }
    }

    return 0; 
}

static const struct ble_gatt_svc_def hid_svcs[] = {
    {
        // Service: Device Information (0x180A)
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            .uuid = BLE_UUID16_DECLARE(0x2A29), // Manufacturer Name
            .access_cb = hid_access_cb,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            0,
        } },
    },
    {
        // Service: Human Interface Device (0x1812)
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),
        .characteristics = (struct ble_gatt_chr_def[]) { {
            // Report Map
            .uuid = BLE_UUID16_DECLARE(0x2A4B),
            .access_cb = hid_access_cb,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            // Report (Input) - Handle saved for notifications
            .uuid = BLE_UUID16_DECLARE(0x2A4D),
            .access_cb = hid_access_cb,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            .val_handle = &hid_report_handle,
        }, {
            // HID Information
            .uuid = BLE_UUID16_DECLARE(0x2A4A),
            .access_cb = hid_access_cb,
            .flags = BLE_GATT_CHR_F_READ,
        }, {
            // Protocol Mode
            .uuid = BLE_UUID16_DECLARE(0x2A4E),
            .access_cb = hid_access_cb,
            .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP,
        }, {
            0,
        } },
    },
    {
        0, 
    }
};

static int ble_hid_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                hid_conn_handle = event->connect.conn_handle;
                is_connected = true;
                ESP_LOGI(TAG, "HID Connected");
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            hid_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            is_connected = false;
            ESP_LOGI(TAG, "HID Disconnected");
            // Auto re-advertise
            ble_hid_init(); 
            break;
    }
    return 0;
}

esp_err_t ble_hid_init(void) {
    // 1. Register Services
    int rc = ble_gatts_count_cfg(hid_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATTS count cfg failed: %d", rc);
        return rc;
    }

    rc = ble_gatts_add_svcs(hid_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATTS add svcs failed: %d", rc);
        return rc;
    }

    // 2. Start Advertising
    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.appearance = 0x03C1; // Keyboard
    fields.name = (uint8_t *)"BadKB-BLE";
    fields.name_len = 9;
    fields.name_is_complete = 1;
    fields.uuids16 = (ble_uuid16_t[]){ BLE_UUID16_INIT(0x1812) };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;

    ble_gap_adv_set_fields(&fields);
    ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL, BLE_HS_FOREVER, &adv_params, ble_hid_gap_event, NULL);
    
    ESP_LOGI(TAG, "HID Services Added & Advertising started");
    return ESP_OK;
}

esp_err_t ble_hid_deinit(void) {
    if (is_connected) {
        ble_gap_terminate(hid_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    ble_gap_adv_stop();
    // NimBLE does not easily support removing services at runtime without reset.
    // We just stop advertising.
    return ESP_OK;
}

bool ble_hid_is_connected(void) {
    return is_connected;
}

void ble_hid_send_key(uint8_t keycode, uint8_t modifier) {
    if (!is_connected) return;

    // Report: Modifier, Reserved, Key1, Key2, Key3, Key4, Key5, Key6
    uint8_t report[8] = {0};
    report[0] = modifier;
    report[2] = keycode;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(report, sizeof(report));
    ble_gattc_notify_custom(hid_conn_handle, hid_report_handle, om);

    vTaskDelay(pdMS_TO_TICKS(15)); // Debounce

    // Release
    memset(report, 0, sizeof(report));
    om = ble_hs_mbuf_from_flat(report, sizeof(report));
    ble_gattc_notify_custom(hid_conn_handle, hid_report_handle, om);
    
    vTaskDelay(pdMS_TO_TICKS(15));
}