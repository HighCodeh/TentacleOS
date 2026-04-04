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

#include "tusb_desc.h"

#include <string.h>

#include "esp_log.h"
#include "driver/gpio.h"
#include "tinyusb.h"

static const char *TAG = "TUSB_DESC";

// USB HID Report IDs
#define HID_REPORT_ID_KEYBOARD 1
#define HID_REPORT_ID_MOUSE    2

// USB Device Identifiers
#define USB_VENDOR_ID  0xCAFE
#define USB_PRODUCT_ID 0x4001
#define USB_BCD_DEVICE 0x0100

// USB Configuration
#define USB_MAX_POWER_MA     100
#define USB_HID_POLL_INTERVAL_MS 1

// String descriptor indices
#define STR_IDX_LANGID       0
#define STR_IDX_MANUFACTURER 1
#define STR_IDX_PRODUCT      2
#define STR_IDX_SERIAL       3

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

// Device Descriptor — USB 2.0, class defined at interface level
static const tusb_desc_device_t s_desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VENDOR_ID,
    .idProduct          = USB_PRODUCT_ID,
    .bcdDevice          = USB_BCD_DEVICE,
    .iManufacturer      = STR_IDX_MANUFACTURER,
    .iProduct           = STR_IDX_PRODUCT,
    .iSerialNumber      = STR_IDX_SERIAL,
    .bNumConfigurations = 1,
};

// HID Report Descriptor — Keyboard (Report ID 1) + Mouse (Report ID 2)
static const uint8_t s_desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_REPORT_ID_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HID_REPORT_ID_MOUSE)),
};

// Configuration Descriptor — 1 interface (HID), remote wakeup enabled
static const uint8_t s_desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1,
                          1,
                          0,
                          CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,
                          USB_MAX_POWER_MA),
    TUD_HID_DESCRIPTOR(TUSB_DESC_ITF_NUM_HID,
                        0,
                        HID_ITF_PROTOCOL_KEYBOARD,
                        sizeof(s_desc_hid_report),
                        0x81,
                        CFG_TUD_HID_EP_BUFSIZE,
                        USB_HID_POLL_INTERVAL_MS),
};

// String Descriptors
static const char *s_string_desc_arr[] = {
    (char[]){0x09, 0x04}, // Language ID: English (US)
    "HighCode",           // Manufacturer
    "BadUSB Device",      // Product
    "123456",             // Serial Number
};

#define STRING_DESC_COUNT (sizeof(s_string_desc_arr) / sizeof(s_string_desc_arr[0]))

static uint16_t s_desc_str_buf[32];

// TinyUSB Descriptor Callbacks

const uint8_t *tud_descriptor_device_cb(void) {
  return (const uint8_t *)&s_desc_device;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  return s_desc_configuration;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void)langid;
  uint8_t chr_count;

  if (index == STR_IDX_LANGID) {
    memcpy(&s_desc_str_buf[1], s_string_desc_arr[0], 2);
    chr_count = 1;
  } else {
    if (index >= STRING_DESC_COUNT) {
      return NULL;
    }
    const char *str = s_string_desc_arr[index];
    chr_count = strlen(str);
    for (uint8_t i = 0; i < chr_count; i++) {
      s_desc_str_buf[1 + i] = str[i];
    }
  }

  s_desc_str_buf[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
  return s_desc_str_buf;
}

const uint8_t *tud_hid_descriptor_report_cb(uint8_t instance) {
  (void)instance;
  return s_desc_hid_report;
}

uint16_t tud_hid_get_report_cb(uint8_t instance,
                                uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer,
                                uint16_t reqlen) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)reqlen;
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance,
                            uint8_t report_id,
                            hid_report_type_t report_type,
                            uint8_t const *buffer,
                            uint16_t bufsize) {
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)bufsize;
}

esp_err_t busb_init(void) {
  ESP_LOGI(TAG, "Initializing TinyUSB driver...");

  // ESP32-P4 High Speed USB requires GPIO ISR service
  esp_err_t err = gpio_install_isr_service(0);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(err));
    return err;
  }
  if (err == ESP_ERR_INVALID_STATE) {
    ESP_LOGD(TAG, "GPIO ISR service already installed");
  }

  const tinyusb_config_t tusb_cfg = {
      .port = TINYUSB_PORT_HIGH_SPEED_0,
      .descriptor =
          {
              .device            = &s_desc_device,
              .string            = s_string_desc_arr,
              .string_count      = STRING_DESC_COUNT,
              .full_speed_config = s_desc_configuration,
              .high_speed_config = s_desc_configuration,
          },
      .phy =
          {
              .skip_setup     = false,
              .self_powered   = false,
              .vbus_monitor_io = -1,
          },
  };

  err = tinyusb_driver_install(&tusb_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to install TinyUSB driver: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "TinyUSB driver installed");
  return ESP_OK;
}
