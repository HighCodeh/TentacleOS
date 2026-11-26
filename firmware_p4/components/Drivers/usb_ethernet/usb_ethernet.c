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

#include "usb_ethernet.h"

#include <stdatomic.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"

#include "sys_prio.h"

#define RX_QUEUE_DEPTH             8
#define USB_STACK_SIZE             6144
#define CONTROL_TIMEOUT_MS         100
#define STOP_TIMEOUT_MS            3000
#define CONTROL_IN                 0xa1
#define CONTROL_OUT                0x21
#define STANDARD_INTERFACE_OUT     0x01
#define STANDARD_INTERFACE_IN      0x81
#define SET_INTERFACE              0x0b
#define GET_INTERFACE              0x0a
#define CLASS_CDC_DATA             0x0a
#define DATA_ENDPOINT_COUNT        2
#define GET_PARAMETERS             0x80
#define SET_NTB_FORMAT             0x84
#define SET_INPUT_SIZE             0x86
#define SET_FILTER                 0x43
#define SET_CRC                    0x8a
#define FILTER_DIRECTED_BROADCAST  0x0c
#define CLASS_CDC                  2
#define SUBCLASS_NCM               13
#define DESC_INTERFACE             4
#define DESC_CS_INTERFACE          0x24
#define DESC_ETHERNET              0x0f
#define DESC_NCM                   0x1a
#define NCM_CAP_CRC                0x10
#define NCM_PARAMETERS_SIZE        28
#define NCM_NTH16_SIZE             12
#define NCM_SINGLE_NDP_SIZE        16
#define MAC_STRING_SIZE            26
#define USB_STRING_REQUEST         6
#define USB_STRING_TYPE            3
#define USB_LANGUAGE_ENGLISH       0x0409
#define ASIX_VID                   0x0b95
#define AX88179B_PID               0x1790
#define AX88179B_NCM_CONFIGURATION 2

static const char *TAG = "USB_ETHERNET";
typedef struct {
  uint16_t length;
  uint8_t data[USB_ETHERNET_FRAME_MAX];
} usb_ethernet_frame_t;
typedef struct {
  uint16_t vid, pid;
  uint8_t address, interface, mac_string, capabilities;
  uint8_t data_interface, data_alternate;
} usb_ethernet_device_t;
static QueueHandle_t s_devices, s_rx;
static StaticQueue_t s_devices_control, s_rx_control;
static usb_ethernet_device_t s_device_storage;
static uint8_t *s_rx_storage, *s_tx;
static cdc_acm_dev_hdl_t s_device;
static usb_ethernet_device_t s_descriptor;
static atomic_bool s_link, s_disconnected, s_stopping, s_daemon_done;
static bool s_host_installed, s_cdc_installed;
static uint8_t s_mac[6];
static uint16_t s_divisor, s_remainder, s_alignment, s_sequence;
static size_t s_tx_limit;

static uint16_t le16(const uint8_t *p) {
  return p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t le32(const uint8_t *p) {
  return le16(p) | ((uint32_t)le16(p + 2) << 16);
}
static void daemon(void *unused) {
  (void)unused;
  while (!atomic_load(&s_stopping)) {
    uint32_t flags = 0;
    usb_host_lib_handle_events(pdMS_TO_TICKS(CONTROL_TIMEOUT_MS), &flags);
    if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
      usb_host_device_free_all();
  }
  atomic_store(&s_daemon_done, true);
  vTaskDeleteWithCaps(NULL);
}
static bool select_configuration(const usb_device_desc_t *desc, uint8_t *out_configuration) {
  if (desc->idVendor == ASIX_VID && desc->idProduct == AX88179B_PID &&
      desc->bNumConfigurations >= AX88179B_NCM_CONFIGURATION) {
    *out_configuration = AX88179B_NCM_CONFIGURATION;
  }
  return true;
}

static bool data_setting(const usb_config_desc_t *config, usb_ethernet_device_t *out_device) {
  const uint8_t *data = (const uint8_t *)config;
  for (size_t p = config->bLength; p + 2 <= config->wTotalLength;) {
    uint8_t size = data[p];
    if (size < 2 || size > config->wTotalLength - p)
      return false;
    if (data[p + 1] == DESC_INTERFACE && size >= sizeof(usb_intf_desc_t) &&
        data[p + 2] == out_device->interface + 1 && data[p + 4] == DATA_ENDPOINT_COUNT &&
        data[p + 5] == CLASS_CDC_DATA) {
      out_device->data_interface = data[p + 2];
      out_device->data_alternate = data[p + 3];
      return true;
    }
    p += size;
  }
  return false;
}

static void detected(usb_device_handle_t device) {
  const usb_config_desc_t *config;
  const usb_device_desc_t *desc;
  usb_device_info_t info;
  if (usb_host_get_active_config_descriptor(device, &config) != ESP_OK ||
      usb_host_get_device_descriptor(device, &desc) != ESP_OK ||
      usb_host_device_info(device, &info) != ESP_OK)
    return;
  usb_ethernet_device_t found = {
      .vid = desc->idVendor, .pid = desc->idProduct, .address = info.dev_addr};
  const uint8_t *data = (const uint8_t *)config;
  bool ncm = false;
  for (size_t p = config->bLength; p + 2 <= config->wTotalLength;) {
    uint8_t size = data[p];
    if (size < 2 || size > config->wTotalLength - p)
      return;
    if (data[p + 1] == DESC_INTERFACE && size >= sizeof(usb_intf_desc_t)) {
      if (ncm && found.mac_string != 0)
        break;
      ncm = data[p + 5] == CLASS_CDC && data[p + 6] == SUBCLASS_NCM;
      if (ncm)
        found.interface = data[p + 2];
    } else if (ncm && data[p + 1] == DESC_CS_INTERFACE && size >= 4) {
      if (data[p + 2] == DESC_ETHERNET)
        found.mac_string = data[p + 3];
      if (data[p + 2] == DESC_NCM && size >= 6)
        found.capabilities = data[p + 5];
    }
    p += size;
  }
  if (ncm && found.mac_string != 0 && data_setting(config, &found)) {
    ESP_LOGI(TAG,
             "USB adapter detected: %04x:%04x configuration=%u CDC-NCM",
             found.vid,
             found.pid,
             config->bConfigurationValue);
    xQueueOverwrite(s_devices, &found);
  } else {
    ESP_LOGW(TAG,
             "USB device %04x:%04x configuration=%u has no supported CDC-NCM interface",
             found.vid,
             found.pid,
             config->bConfigurationValue);
  }
}
static bool frame_received(const uint8_t *frame, size_t length, void *context) {
  (void)context;
  // A bounded stack copy avoids lending the USB DMA buffer to the SPI worker.
  usb_ethernet_frame_t packet = {.length = length};
  memcpy(packet.data, frame, length);
  return xQueueSend(s_rx, &packet, 0) == pdTRUE;
}
static bool received(const uint8_t *data, size_t length, void *context) {
  (void)context;
  if (length >= 12 && le32(data) == UINT32_C(0x484d434e) && le16(data + 8) > length &&
      le16(data + 8) <= USB_ETHERNET_NTB_SIZE)
    return false;
  if (atomic_load(&s_link))
    (void)usb_ethernet_ntb_decode(data, length, frame_received, NULL);
  return true;
}
static void event(const cdc_acm_host_dev_event_data_t *event, void *context) {
  (void)context;
  if (event->type == CDC_ACM_HOST_NETWORK_CONNECTION) {
    bool was_connected = atomic_exchange(&s_link, event->data.network_connected);
    if (was_connected != event->data.network_connected)
      ESP_LOGI(TAG, "Ethernet link %s", event->data.network_connected ? "up" : "down");
    if (!event->data.network_connected)
      xQueueReset(s_rx);
  }
  if (event->type == CDC_ACM_HOST_DEVICE_DISCONNECTED || event->type == CDC_ACM_HOST_ERROR) {
    atomic_store(&s_link, false);
    atomic_store(&s_disconnected, true);
  }
}
static esp_err_t
control(uint8_t direction, uint8_t request, uint16_t value, uint16_t length, uint8_t *data) {
  return cdc_acm_host_send_custom_request(
      s_device, direction, request, value, s_descriptor.interface, length, data);
}
static int hex(uint8_t ch) {
  if (ch >= '0' && ch <= '9')
    return ch - '0';
  if (ch >= 'a' && ch <= 'f')
    return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F')
    return ch - 'A' + 10;
  return -1;
}
static esp_err_t configure(void) {
  uint8_t parameters[NCM_PARAMETERS_SIZE] = {0}, string[MAC_STRING_SIZE] = {0};
  esp_err_t ret = control(CONTROL_IN, GET_PARAMETERS, 0, sizeof(parameters), parameters);
  if (ret != ESP_OK)
    return ret;
  if (le16(parameters) < NCM_PARAMETERS_SIZE || !(le16(parameters + 2) & 1) ||
      le32(parameters + 4) < USB_ETHERNET_NTB_SIZE)
    return ESP_ERR_NOT_SUPPORTED;
  s_tx_limit = le32(parameters + 16);
  if (s_tx_limit > USB_ETHERNET_NTB_SIZE)
    s_tx_limit = USB_ETHERNET_NTB_SIZE;
  s_divisor = le16(parameters + 20);
  s_remainder = le16(parameters + 22);
  s_alignment = le16(parameters + 24);
  if (s_divisor == 0 || s_divisor > 512 || s_remainder >= s_divisor || s_alignment == 0 ||
      s_alignment > 512 || s_tx_limit < USB_ETHERNET_FRAME_MAX + 32)
    return ESP_ERR_NOT_SUPPORTED;
  size_t table = (NCM_NTH16_SIZE + s_alignment - 1) / s_alignment * s_alignment;
  size_t offset = table + NCM_SINGLE_NDP_SIZE;
  offset += (s_remainder + s_divisor - ((offset + USB_ETHERNET_FRAME_MIN) % s_divisor)) % s_divisor;
  if (offset + USB_ETHERNET_FRAME_MAX + 1 > s_tx_limit)
    return ESP_ERR_NOT_SUPPORTED;
  ret = cdc_acm_host_send_custom_request(s_device,
                                         0x80,
                                         USB_STRING_REQUEST,
                                         (USB_STRING_TYPE << 8) | s_descriptor.mac_string,
                                         USB_LANGUAGE_ENGLISH,
                                         sizeof(string),
                                         string);
  if (ret != ESP_OK)
    return ret;
  if (string[0] != MAC_STRING_SIZE || string[1] != USB_STRING_TYPE)
    return ESP_ERR_INVALID_RESPONSE;
  for (size_t i = 0; i < sizeof(s_mac); ++i) {
    int high = hex(string[2 + i * 4]), low = hex(string[4 + i * 4]);
    if (high < 0 || low < 0 || string[3 + i * 4] || string[5 + i * 4])
      return ESP_ERR_INVALID_RESPONSE;
    s_mac[i] = (high << 4) | low;
  }
  if (s_mac[0] & 1)
    return ESP_ERR_INVALID_RESPONSE;
  ret = ESP_OK;
  if (le16(parameters + 2) & 2)
    ret = control(CONTROL_OUT, SET_NTB_FORMAT, 0, 0, NULL);
  if (ret == ESP_OK && (s_descriptor.capabilities & NCM_CAP_CRC))
    ret = control(CONTROL_OUT, SET_CRC, 0, 0, NULL);
  uint8_t size[] = {0, USB_ETHERNET_NTB_SIZE >> 8, 0, 0};
  if (ret == ESP_OK)
    ret = control(CONTROL_OUT, SET_INPUT_SIZE, 0, sizeof(size), size);
  if (ret != ESP_OK)
    return ret;
  ESP_LOGI(TAG,
           "Activating CDC-NCM data interface %u alternate %u",
           s_descriptor.data_interface,
           s_descriptor.data_alternate);
  ret = cdc_acm_host_send_custom_request(s_device,
                                         STANDARD_INTERFACE_OUT,
                                         SET_INTERFACE,
                                         s_descriptor.data_alternate,
                                         s_descriptor.data_interface,
                                         0,
                                         NULL);
  if (ret != ESP_OK)
    return ret;
  uint8_t alternate = 0;
  ret = cdc_acm_host_send_custom_request(s_device,
                                         STANDARD_INTERFACE_IN,
                                         GET_INTERFACE,
                                         0,
                                         s_descriptor.data_interface,
                                         sizeof(alternate),
                                         &alternate);
  if (ret != ESP_OK)
    return ret;
  if (alternate != s_descriptor.data_alternate)
    return ESP_ERR_INVALID_RESPONSE;
  ret = control(CONTROL_OUT, SET_FILTER, FILTER_DIRECTED_BROADCAST, 0, NULL);
  return ret;
}

esp_err_t usb_ethernet_start(void) {
  if (s_host_installed)
    return ESP_ERR_INVALID_STATE;
  s_rx_storage = heap_caps_malloc(RX_QUEUE_DEPTH * sizeof(usb_ethernet_frame_t), MALLOC_CAP_SPIRAM);
  s_tx = heap_caps_malloc(USB_ETHERNET_NTB_SIZE, MALLOC_CAP_SPIRAM);
  if (s_rx_storage == NULL || s_tx == NULL) {
    ESP_LOGE(TAG, "Packet buffer allocation failed");
    free(s_rx_storage);
    free(s_tx);
    s_rx_storage = NULL;
    s_tx = NULL;
    return ESP_ERR_NO_MEM;
  }
  s_devices = xQueueCreateStatic(
      1, sizeof(s_device_storage), (uint8_t *)&s_device_storage, &s_devices_control);
  s_rx =
      xQueueCreateStatic(RX_QUEUE_DEPTH, sizeof(usb_ethernet_frame_t), s_rx_storage, &s_rx_control);
  atomic_store(&s_stopping, false);
  atomic_store(&s_daemon_done, false);
  atomic_store(&s_disconnected, false);
  atomic_store(&s_link, false);
  usb_host_config_t config = {.intr_flags = ESP_INTR_FLAG_LEVEL1,
                              .enum_filter_cb = select_configuration};
  esp_err_t ret = usb_host_install(&config);
  if (ret != ESP_OK)
    goto fail;
  s_host_installed = true;
  // This event task performs USB work only; no flash or filesystem writes.
  if (xTaskCreatePinnedToCoreWithCaps(daemon,
                                      "usb_eth_host",
                                      USB_STACK_SIZE,
                                      NULL,
                                      SYS_PRIO_SERVICE_HI,
                                      NULL,
                                      SYS_CORE_RADIO,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    atomic_store(&s_daemon_done, true);
    ret = ESP_ERR_NO_MEM;
    goto fail;
  }
  cdc_acm_host_driver_config_t cdc = {.driver_task_stack_size = USB_STACK_SIZE,
                                      .driver_task_priority = SYS_PRIO_SERVICE_HI,
                                      .xCoreID = SYS_CORE_RADIO,
                                      .new_dev_cb = detected};
  ret = cdc_acm_host_install(&cdc);
  if (ret != ESP_OK)
    goto fail;
  s_cdc_installed = true;
  ESP_LOGI(TAG,
           "Host ready: internal free=%u largest=%u",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  return ESP_OK;
fail:
  (void)usb_ethernet_stop();
  return ret;
}

esp_err_t usb_ethernet_poll(void) {
  if (atomic_exchange(&s_disconnected, false) && s_device != NULL) {
    esp_err_t closed = cdc_acm_host_close(s_device);
    if (closed != ESP_OK) {
      atomic_store(&s_disconnected, true);
      return closed;
    }
    s_device = NULL;
    xQueueReset(s_rx);
  }
  if (s_device != NULL || xQueueReceive(s_devices, &s_descriptor, 0) != pdTRUE)
    return ESP_OK;
  cdc_acm_host_open_config_t config = {.vid = s_descriptor.vid,
                                       .pid = s_descriptor.pid,
                                       .dev_addr = s_descriptor.address,
                                       .interface_idx = s_descriptor.interface,
                                       .connection_timeout_ms = CONTROL_TIMEOUT_MS,
                                       .in_buffer_size = USB_ETHERNET_NTB_SIZE,
                                       .out_buffer_size = USB_ETHERNET_NTB_SIZE,
                                       .data_cb = received,
                                       .event_cb = event};
  ESP_LOGI(TAG, "Opening CDC-NCM interface %u", s_descriptor.interface);
  esp_err_t ret = cdc_acm_host_open(&config, &s_device);
  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "Configuring CDC-NCM transfers");
    ret = configure();
  }
  if (ret != ESP_OK)
    ESP_LOGE(TAG, "CDC-NCM setup failed: %s", esp_err_to_name(ret));
  if (ret != ESP_OK && s_device != NULL) {
    esp_err_t closed = cdc_acm_host_close(s_device);
    if (closed == ESP_OK)
      s_device = NULL;
    atomic_store(&s_link, false);
  }
  if (ret == ESP_OK)
    ESP_LOGI(TAG,
             "CDC-NCM adapter %04x:%04x ready; internal free=%u largest=%u",
             s_descriptor.vid,
             s_descriptor.pid,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  return ret;
}

bool usb_ethernet_identity(uint16_t *out_vid, uint16_t *out_pid) {
  if (s_device == NULL || out_vid == NULL || out_pid == NULL)
    return false;
  *out_vid = s_descriptor.vid;
  *out_pid = s_descriptor.pid;
  return true;
}

bool usb_ethernet_link(uint8_t out_mac[6]) {
  if (s_device == NULL || !atomic_load(&s_link))
    return false;
  memcpy(out_mac, s_mac, sizeof(s_mac));
  return true;
}
size_t usb_ethernet_receive(uint8_t *out_frame) {
  usb_ethernet_frame_t packet;
  if (s_rx == NULL || xQueueReceive(s_rx, &packet, 0) != pdTRUE)
    return 0;
  memcpy(out_frame, packet.data, packet.length);
  return packet.length;
}
esp_err_t usb_ethernet_transmit(const uint8_t *frame, size_t length) {
  if (s_device == NULL || !atomic_load(&s_link))
    return ESP_ERR_INVALID_STATE;
  size_t size = usb_ethernet_ntb_encode(
      s_tx, s_tx_limit, frame, length, s_sequence++, s_divisor, s_remainder, s_alignment);
  if (size == 0)
    return ESP_ERR_INVALID_SIZE;
  return cdc_acm_host_data_tx_blocking(s_device, s_tx, size, CONTROL_TIMEOUT_MS);
}
esp_err_t usb_ethernet_stop(void) {
  atomic_store(&s_link, false);
  if (s_device != NULL) {
    esp_err_t ret = cdc_acm_host_close(s_device);
    if (ret != ESP_OK)
      return ret;
    s_device = NULL;
  }
  if (s_cdc_installed) {
    esp_err_t ret = cdc_acm_host_uninstall();
    if (ret != ESP_OK)
      return ret;
    s_cdc_installed = false;
  }
  if (s_host_installed) {
    for (unsigned wait = 0; wait < STOP_TIMEOUT_MS; wait += CONTROL_TIMEOUT_MS) {
      usb_host_device_free_all();
      usb_host_lib_info_t info;
      if (usb_host_lib_info(&info) == ESP_OK && info.num_devices == 0)
        break;
      vTaskDelay(pdMS_TO_TICKS(CONTROL_TIMEOUT_MS));
    }
    usb_host_lib_info_t remaining;
    if (usb_host_lib_info(&remaining) != ESP_OK || remaining.num_devices != 0)
      return ESP_ERR_TIMEOUT;
    atomic_store(&s_stopping, true);
    while (!atomic_load(&s_daemon_done))
      vTaskDelay(1);
    esp_err_t ret = usb_host_uninstall();
    if (ret != ESP_OK)
      return ret;
    s_host_installed = false;
  }
  if (s_rx)
    vQueueDelete(s_rx);
  if (s_devices)
    vQueueDelete(s_devices);
  s_rx = NULL;
  s_devices = NULL;
  free(s_rx_storage);
  free(s_tx);
  s_rx_storage = NULL;
  s_tx = NULL;
  return ESP_OK;
}
