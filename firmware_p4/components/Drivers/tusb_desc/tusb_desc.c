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

#include "tusb_desc.h"

#include <string.h>

#include "esp_log.h"
#include "driver/gpio.h"
#include "pin_def.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"

static const char *TAG = "TUSB_DESC";

// USB HID Report IDs
#define HID_REPORT_ID_KEYBOARD 1
#define HID_REPORT_ID_MOUSE    2

// USB Device Identifiers
#define USB_VENDOR_ID  0xCAFE
#define USB_PRODUCT_ID 0x4001
#define USB_BCD_DEVICE 0x0100

// USB Configuration
#define USB_MAX_POWER_MA         100
#define USB_HID_POLL_INTERVAL_MS 1

// String descriptor indices
#define STR_IDX_LANGID       0
#define STR_IDX_MANUFACTURER 1
#define STR_IDX_PRODUCT      2
#define STR_IDX_SERIAL       3
#define STR_IDX_CDC          4

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)

// CDC data (bulk) endpoint max packet size is speed-dependent: USB requires it
// to be EXACTLY 512 at High Speed and 8/16/32/64 at Full Speed. The P4 USB is
// High Speed, so a 64-byte value here makes tu_edpt_validate reject the config
// (cdcd_open -> SET_CONFIGURATION fails), which also takes the HID keyboard
// down. We build one descriptor per speed and serve the right one below.
#define CDC_EP_SIZE_HS 512
#define CDC_EP_SIZE_FS 64

// Device Descriptor — USB 2.0 composite (HID + CDC). The CDC IAD requires the
// Miscellaneous device class so the host groups the CDC interfaces correctly.
static const tusb_desc_device_t s_desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VENDOR_ID,
    .idProduct = USB_PRODUCT_ID,
    .bcdDevice = USB_BCD_DEVICE,
    .iManufacturer = STR_IDX_MANUFACTURER,
    .iProduct = STR_IDX_PRODUCT,
    .iSerialNumber = STR_IDX_SERIAL,
    .bNumConfigurations = 1,
};

// HID Report Descriptor — Keyboard (Report ID 1) + Mouse (Report ID 2)
static const uint8_t s_desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_REPORT_ID_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HID_REPORT_ID_MOUSE)),
};

// Configuration Descriptor — composite: HID (BadUSB) + CDC-ACM (companion link).
// Identical for both speeds except the CDC bulk endpoint size (see above).
#define CONFIG_DESCRIPTOR(cdc_ep_size)                                                             \
  TUD_CONFIG_DESCRIPTOR(1,                                                                          \
                        TUSB_DESC_ITF_NUM_TOTAL,                                                    \
                        0,                                                                          \
                        CONFIG_TOTAL_LEN,                                                           \
                        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,                                         \
                        USB_MAX_POWER_MA),                                                          \
      TUD_HID_DESCRIPTOR(TUSB_DESC_ITF_NUM_HID,                                                     \
                         0,                                                                         \
                         HID_ITF_PROTOCOL_KEYBOARD,                                                 \
                         sizeof(s_desc_hid_report),                                                 \
                         TUSB_DESC_EP_HID_IN,                                                        \
                         CFG_TUD_HID_EP_BUFSIZE,                                                     \
                         USB_HID_POLL_INTERVAL_MS),                                                 \
      TUD_CDC_DESCRIPTOR(TUSB_DESC_ITF_NUM_CDC,                                                     \
                         STR_IDX_CDC,                                                               \
                         TUSB_DESC_EP_CDC_NOTIF,                                                     \
                         8,                                                                         \
                         TUSB_DESC_EP_CDC_OUT,                                                       \
                         TUSB_DESC_EP_CDC_IN,                                                        \
                         (cdc_ep_size))

static const uint8_t s_desc_configuration_hs[] = {CONFIG_DESCRIPTOR(CDC_EP_SIZE_HS)};
static const uint8_t s_desc_configuration_fs[] = {CONFIG_DESCRIPTOR(CDC_EP_SIZE_FS)};

// String Descriptors
static const char *s_string_desc_arr[] = {
    (char[]){0x09, 0x04},   // Language ID: English (US)
    "HighCode",             // Manufacturer
    "BadUSB Device",        // Product
    "123456",               // Serial Number
    "TentacleOS Companion", // CDC interface (host link)
};

#define STRING_DESC_COUNT (sizeof(s_string_desc_arr) / sizeof(s_string_desc_arr[0]))

static uint16_t s_desc_str_buf[32];

// TinyUSB Descriptor Callbacks

const uint8_t *tud_descriptor_device_cb(void) {
  return (const uint8_t *)&s_desc_device;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  // Serve the descriptor whose CDC bulk endpoint size matches the negotiated
  // link speed (512 at HS, 64 at FS), so tu_edpt_validate accepts the config.
  return (tud_speed_get() == TUSB_SPEED_HIGH) ? s_desc_configuration_hs : s_desc_configuration_fs;
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
  // HID (BadUSB) and CDC (companion) share one TinyUSB install — whoever calls
  // first brings the composite up; later calls are no-ops.
  static bool s_installed = false;
  if (s_installed) {
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Initializing TinyUSB driver...");

  // Route the USB-C D+/D- mux (TS3USB221) to the P4 native USB PHY. The select
  // line (GPIO19) has a 10k pulldown that defaults the Type-C to the CP2105
  // USB-UART bridge, so the native USB never enumerates until we drive it high.
  // Note: this is a shared single Type-C - switching to native USB takes the
  // USB-serial console/flash path off that connector until the next reset (the
  // ROM download mode runs before this, so flashing over USB-C still works).
  gpio_config_t usb_mux_cfg = {
      .pin_bit_mask = 1ULL << GPIO_USB_MUX_SEL_PIN,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&usb_mux_cfg);
  gpio_set_level(GPIO_USB_MUX_SEL_PIN, 1);

  // ESP32-P4 High Speed USB requires GPIO ISR service. It may already be up
  // (buttons_init installs it earlier at boot), in which case the driver logs
  // an ERROR before returning ESP_ERR_INVALID_STATE — harmless for us, so we
  // silence the "gpio" tag around the call and treat "already installed" as OK.
  esp_log_level_t gpio_log_level = esp_log_level_get("gpio");
  esp_log_level_set("gpio", ESP_LOG_NONE);
  esp_err_t err = gpio_install_isr_service(0);
  esp_log_level_set("gpio", gpio_log_level);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(err));
    return err;
  }
  if (err == ESP_ERR_INVALID_STATE) {
    ESP_LOGD(TAG, "GPIO ISR service already installed");
  }

  const tinyusb_config_t tusb_cfg = {
      .port = TINYUSB_PORT_HIGH_SPEED_0,
      .task =
          {
              .size = TINYUSB_DEFAULT_TASK_SIZE,
              .priority = TINYUSB_DEFAULT_TASK_PRIO,
              .xCoreID = TINYUSB_DEFAULT_TASK_AFFINITY,
          },
      .descriptor =
          {
              .device = &s_desc_device,
              .string = s_string_desc_arr,
              .string_count = STRING_DESC_COUNT,
              .full_speed_config = s_desc_configuration_fs,
              .high_speed_config = s_desc_configuration_hs,
          },
      .phy =
          {
              .skip_setup = false,
              .self_powered = false,
              .vbus_monitor_io = -1,
          },
  };

  err = tinyusb_driver_install(&tusb_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to install TinyUSB driver: %s", esp_err_to_name(err));
    return err;
  }

  s_installed = true;
  ESP_LOGI(TAG, "TinyUSB driver installed");
  return ESP_OK;
}
