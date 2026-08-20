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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pin_def.h"
#include "soc/usb_dwc_struct.h"
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
#define BUSB_REENUM_DELAY_MS     100 // detach window so the host notices the re-enumeration

// String descriptor indices
#define STR_IDX_LANGID       0
#define STR_IDX_MANUFACTURER 1
#define STR_IDX_PRODUCT      2
#define STR_IDX_SERIAL       3
#define STR_IDX_CDC          4
#if CFG_TUD_MSC
#define STR_IDX_MSC 5
#endif

// MSC is a runtime-selected variant, exposed only in mass-storage mode: an
// unbacked MSC LUN crashes the TinyUSB task on the host's first SCSI command.
#define ITF_NUM_BASE    3 // HID + CDC (comm + data)
#define CONFIG_LEN_BASE (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_CDC_DESC_LEN)

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
#define HID_CDC_BLOCK(itf_total, total_len, cdc_ep_size)                                     \
  TUD_CONFIG_DESCRIPTOR(                                                                     \
      1, (itf_total), 0, (total_len), TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, USB_MAX_POWER_MA), \
      TUD_HID_DESCRIPTOR(TUSB_DESC_ITF_NUM_HID,                                              \
                         0,                                                                  \
                         HID_ITF_PROTOCOL_KEYBOARD,                                          \
                         sizeof(s_desc_hid_report),                                          \
                         TUSB_DESC_EP_HID_IN,                                                \
                         CFG_TUD_HID_EP_BUFSIZE,                                             \
                         USB_HID_POLL_INTERVAL_MS),                                          \
      TUD_CDC_DESCRIPTOR(TUSB_DESC_ITF_NUM_CDC,                                              \
                         STR_IDX_CDC,                                                        \
                         TUSB_DESC_EP_CDC_NOTIF,                                             \
                         8,                                                                  \
                         TUSB_DESC_EP_CDC_OUT,                                               \
                         TUSB_DESC_EP_CDC_IN,                                                \
                         (cdc_ep_size))

static const uint8_t s_desc_configuration_hs[] = {
    HID_CDC_BLOCK(ITF_NUM_BASE, CONFIG_LEN_BASE, CDC_EP_SIZE_HS)};
static const uint8_t s_desc_configuration_fs[] = {
    HID_CDC_BLOCK(ITF_NUM_BASE, CONFIG_LEN_BASE, CDC_EP_SIZE_FS)};

#if CFG_TUD_MSC
#define ITF_NUM_MSC_TOTAL 4
#define CONFIG_LEN_MSC    (CONFIG_LEN_BASE + TUD_MSC_DESC_LEN)
#define MSC_INTERFACE(ep_size) \
  TUD_MSC_DESCRIPTOR(          \
      TUSB_DESC_ITF_NUM_MSC, STR_IDX_MSC, TUSB_DESC_EP_MSC_OUT, TUSB_DESC_EP_MSC_IN, (ep_size))

static const uint8_t s_desc_configuration_msc_hs[] = {
    HID_CDC_BLOCK(ITF_NUM_MSC_TOTAL, CONFIG_LEN_MSC, CDC_EP_SIZE_HS),
    MSC_INTERFACE(CDC_EP_SIZE_HS)};
static const uint8_t s_desc_configuration_msc_fs[] = {
    HID_CDC_BLOCK(ITF_NUM_MSC_TOTAL, CONFIG_LEN_MSC, CDC_EP_SIZE_FS),
    MSC_INTERFACE(CDC_EP_SIZE_FS)};
#endif

// String Descriptors
static const char *s_string_desc_arr[] = {
    (char[]){0x09, 0x04},   // Language ID: English (US)
    "HighCode",             // Manufacturer
    "BadUSB Device",        // Product
    "123456",               // Serial Number
    "TentacleOS Companion", // CDC interface (host link)
#if CFG_TUD_MSC
    "TentacleOS SD",
#endif
};

#define STRING_DESC_COUNT (sizeof(s_string_desc_arr) / sizeof(s_string_desc_arr[0]))

static uint16_t s_desc_str_buf[32];

// Whether the composite currently advertises the MSC interface (mass-storage
// mode). Off by default so plain native-USB bring-ups stay HID+CDC only.
static volatile bool s_msc_exposed = false;

// TinyUSB Descriptor Callbacks

const uint8_t *tud_descriptor_device_cb(void) {
  return (const uint8_t *)&s_desc_device;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
  (void)index;
  // Serve the descriptor whose CDC bulk endpoint size matches the negotiated
  // link speed (512 at HS, 64 at FS), so tu_edpt_validate accepts the config.
  bool high_speed = (tud_speed_get() == TUSB_SPEED_HIGH);
#if CFG_TUD_MSC
  if (s_msc_exposed) {
    return high_speed ? s_desc_configuration_msc_hs : s_desc_configuration_msc_fs;
  }
#endif
  return high_speed ? s_desc_configuration_hs : s_desc_configuration_fs;
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

static void force_hs_otg_session_valid(void) {
  usb_dwc_gotgctl_reg_t otg = USB_DWC_HS.gotgctl_reg;
  otg.vbvalidoven = 1;
  otg.vbvalidovval = 1;
  otg.bvalidoven = 1;
  otg.bvalidovval = 1;
  USB_DWC_HS.gotgctl_reg = otg;
}

static bool s_installed = false;

esp_err_t busb_init(void) {
  // HID (BadUSB) and CDC (companion) share one TinyUSB install — whoever calls
  // first brings the composite up; later calls are no-ops.
  if (s_installed) {
    return ESP_OK;
  }

  ESP_LOGI(TAG, "Initializing TinyUSB driver...");

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
              // Item 41c: kept bus-powered. A battery device is "more correctly"
              // self_powered = true, but esp_tinyusb self-powered mode needs a
              // vbus_monitor_io GPIO to detect VBUS attach/detach, and no such
              // pin is routed to the P4 on this board (the BQ25896 reports VBUS
              // over I2C). The power manager gets VBUS truth from the charger
              // (item 41a) instead, so this stays bus-powered.
              .self_powered = false,
              .vbus_monitor_io = -1,
          },
  };

  esp_err_t err = tinyusb_driver_install(&tusb_cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to install TinyUSB driver: %s", esp_err_to_name(err));
    return err;
  }

  force_hs_otg_session_valid();

  s_installed = true;
  ESP_LOGI(TAG, "TinyUSB driver installed");
  return ESP_OK;
}

void busb_set_msc_exposed(bool exposed) {
#if CFG_TUD_MSC
  if (s_msc_exposed == exposed) {
    return;
  }
  // Not up yet: the flag alone decides what the first enumeration advertises.
  if (!s_installed) {
    s_msc_exposed = exposed;
    return;
  }
  // Already enumerated with the other layout; drop off the bus, swap the
  // advertised config, and re-attach so the host re-reads the descriptor.
  tud_disconnect();
  vTaskDelay(pdMS_TO_TICKS(BUSB_REENUM_DELAY_MS));
  s_msc_exposed = exposed;
  tud_connect();
#else
  (void)exposed;
#endif
}

// USB-C data mux (TS3USB221) on GPIO_USB_MUX_SEL_PIN. LOW routes the single
// Type-C to the CP2105 USB-UART bridge (serial console / flashing); HIGH routes
// it to the P4 native USB PHY (TinyUSB HID + CDC). The two share one connector,
// so only one is live at a time. Switching is runtime and needs no reset.

static bool s_mux_native = false;
static bool s_mux_configured = false;

static void usb_mux_configure_pin(void) {
  if (s_mux_configured) {
    return;
  }
  gpio_config_t cfg = {
      .pin_bit_mask = 1ULL << GPIO_USB_MUX_SEL_PIN,
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&cfg);
  s_mux_configured = true;
}

void usb_mux_init(void) {
  usb_mux_configure_pin();
  gpio_set_level(GPIO_USB_MUX_SEL_PIN, 0); // default: USB-UART bridge
  s_mux_native = false;
  ESP_LOGI(TAG, "USB mux default: UART bridge");
}

esp_err_t usb_mux_set_native(bool native) {
  usb_mux_configure_pin();
  gpio_set_level(GPIO_USB_MUX_SEL_PIN, native ? 1 : 0);
  s_mux_native = native;
  if (native) {
    esp_err_t err = busb_init();
    if (err != ESP_OK) {
      return err;
    }
  }
  ESP_LOGI(TAG, "USB mux -> %s", native ? "native P4 USB" : "UART bridge");
  return ESP_OK;
}

bool usb_mux_is_native(void) {
  return s_mux_native;
}
