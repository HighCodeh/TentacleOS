/* Experimental High Boy integration. Default OFF; registration has no effects.
 * Existing C5 Wi-Fi/NVS owners remain authoritative. No new radio or board init.
 */
#include "hb_muse_c5_service.h"
#include "hb_muse_rpc.h"
#include "hb_muse_setup.h"
#include "hb_muse_pairing_gatt.h"
#include "hb_muse_token_uart.h"
#include "wifi_service.h"
#include "bluetooth_service.h"
#include "ble_hid_keyboard.h"
#include "meshcore_gatt.h"
#include "meshtastic_gatt.h"
#include "host_link_gatt.h"
#include "grok_wifi.h"
#include "ota_service.h"
#include "ota_version.h"
#include "session_manager.h"
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>

_Static_assert((int)SPI_STATUS_OK == (int)HB_MUSE_RPC_OK, "SPI status ABI");
_Static_assert((int)SPI_STATUS_BUSY == (int)HB_MUSE_RPC_BUSY, "SPI status ABI");
_Static_assert((int)SPI_STATUS_ERROR == (int)HB_MUSE_RPC_ERROR, "SPI status ABI");
_Static_assert((int)SPI_STATUS_UNSUPPORTED == (int)HB_MUSE_RPC_UNSUPPORTED, "SPI status ABI");
_Static_assert((int)SPI_STATUS_INVALID_ARG == (int)HB_MUSE_RPC_INVALID, "SPI status ABI");
_Static_assert(SPI_MAX_PAYLOAD - SPI_RESP_STATUS_SIZE >= HB_MUSE_RUNTIME_FRAME_MAX,
               "Muse inner frame must fit ordinary SPI reply");
static hb_muse_rpc_t s_rpc;
static atomic_bool s_initialized;
static bool s_registered, s_init_attempted;
static bool s_setup_initialized, s_setup_attempted;
static atomic_bool s_bt_claimed, s_bt_fault;
static atomic_uint s_setup_handle = ATOMIC_VAR_INIT(UINT16_MAX);
static void (*s_previous_reset)(int);
static char s_node[32], s_mac[18], s_device[48], s_name[32];
bool hb_muse_wifi_provision_guarded(const char *, const char *, bool (*)(void *), void *);
bool hb_muse_wifi_scan_guarded(
    hb_muse_pairing_network_t *, size_t, size_t *, bool (*)(void *), void *);
static bool initialize_setup(void);
/* Setup-worker diagnostics only; never print peer data or secret strings. */
static void bt_trace(const char *stage, int code) {
  ESP_LOGI(
      "MUSE_BT",
      "stage=%s code=%d internal=%u largest=%u dma=%u dma_largest=%u",
      stage,
      code,
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT |
                                                 MALLOC_CAP_DMA));
}
static bool wifi_connected(void *unused) {
  (void)unused;
  return wifi_service_is_connected();
}
static uint64_t now_ms(void *unused) {
  (void)unused;
  return (uint64_t)esp_timer_get_time() / 1000u;
}
static bool ensure_initialized(void *unused) {
  (void)unused;
  if (atomic_load(&s_initialized))
    return true;
  if (s_init_attempted)
    return false; /* partial init is latched, no replay */
  s_init_attempted = true;
  uint8_t mac[6];
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK)
    return false;
  /* Same backend node format as upstream identity.c, not a minted account. */
  snprintf(s_node, sizeof(s_node), "homelink-%02x%02x%02x", mac[3], mac[4], mac[5]);
  snprintf(s_mac,
           sizeof(s_mac),
           "%02x:%02x:%02x:%02x:%02x:%02x",
           mac[0],
           mac[1],
           mac[2],
           mac[3],
           mac[4],
           mac[5]);
  snprintf(s_device, sizeof(s_device), "hatch-link:%s", s_mac);
  snprintf(s_name, sizeof(s_name), "MuseGadget-%02X%02X%02X", mac[3], mac[4], mac[5]);
  uint32_t boot_id = esp_random();
  if (!boot_id)
    boot_id = esp_random();
  if (!boot_id)
    return false;
  hb_muse_runtime_config_t config = {.boot_id = boot_id,
                                     .existing_device_id = s_node,
                                     .wifi_connected = wifi_connected,
                                     .now_ms = now_ms};
  bool ok = hb_muse_runtime_init(&config);
  atomic_store(&s_initialized, ok);
  return ok;
}
static bool enable(void *u, bool value) {
  (void)u;
  return hb_muse_runtime_set_enabled(value);
}
static bool connect_cloud(void *u) {
  (void)u;
  return hb_muse_runtime_connect();
}
static void revoke(void *u) {
  (void)u;
  hb_muse_runtime_revoke();
}
static bool status(void *u, hb_muse_runtime_status_t *s) {
  (void)u;
  return hb_muse_runtime_status(s);
}
static bool submit(void *u, const uint8_t *p, size_t n) {
  (void)u;
  return hb_muse_runtime_submit(p, n);
}
static bool take_reply(void *u, uint8_t *p, size_t cap, size_t *n) {
  (void)u;
  return hb_muse_runtime_take_reply(p, cap, n);
}
static bool ota_quiet(void) {
  spi_ota_status_t s = {0};
  ota_service_get_status(&s);
  return s.state == SPI_OTA_STATE_IDLE || s.state == SPI_OTA_STATE_ERROR;
}
static bool competing_profiles(void) {
  return meshcore_gatt_is_running() || meshtastic_gatt_is_running() || host_link_gatt_is_running();
}
static bool services_quiet(void *u) {
  (void)u;
  return atomic_load(&s_bt_claimed) && !atomic_load(&s_bt_fault) && !grok_wifi_is_active() &&
         !competing_profiles() && !spi_bridge_async_scan_busy() && !session_manager_is_active() &&
         ota_quiet();
}
static void setup_reset(int reason) {
  hb_muse_setup_revoke();
  if (s_previous_reset)
    s_previous_reset(reason);
}
static int setup_gap(struct ble_gap_event *event, void *u) {
  (void)u;
  if (!event)
    return 0;
  switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
      if (event->connect.status == 0) {
        uint16_t handle = event->connect.conn_handle;
        if (!hb_muse_setup_gap_connected(handle, ble_att_mtu(handle)))
          (void)ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
        else
          atomic_store(&s_setup_handle, handle);
      } else
        hb_muse_setup_revoke();
      break;
    case BLE_GAP_EVENT_SUBSCRIBE:
      if (event->subscribe.attr_handle == hb_muse_pairing_gatt_tx_handle())
        hb_muse_setup_gap_subscribed(event->subscribe.conn_handle,
                                     event->subscribe.cur_notify,
                                     ble_att_mtu(event->subscribe.conn_handle));
      break;
    case BLE_GAP_EVENT_DISCONNECT:
      hb_muse_setup_gap_disconnected(event->disconnect.conn.conn_handle);
      if (atomic_load(&s_setup_handle) == event->disconnect.conn.conn_handle)
        atomic_store(&s_setup_handle, UINT16_MAX);
      break;
    default:
      break;
  }
  return 0;
}
static void release_ble(void *u) {
  (void)u;
  if (!atomic_load(&s_bt_claimed))
    return;
  if (bluetooth_service_is_running()) {
    (void)ble_gap_adv_stop();
    unsigned handle = atomic_load(&s_setup_handle);
    if (handle != UINT16_MAX)
      (void)ble_gap_terminate((uint16_t)handle, BLE_ERR_REM_USER_CONN_TERM);
    if (bluetooth_service_stop() != ESP_OK) {
      atomic_store(&s_bt_fault, true);
      return;
    }
  }
  if (bluetooth_service_deinit() != ESP_OK) {
    atomic_store(&s_bt_fault, true);
    return;
  }
  ble_hid_stack_reset();
  atomic_store(&s_setup_handle, UINT16_MAX);
  s_previous_reset = NULL;
  /* Actual stopped/deinitialized host is the registration fence. */
  if (!hb_muse_setup_stack_reset()) {
    atomic_store(&s_bt_fault, true);
    return;
  }
  atomic_store(&s_bt_claimed, false);
}
static bool acquire_ble(void *u, bool (*registration)(void)) {
  (void)u;
  unsigned blocked = (!registration ? 1u : 0u) | (atomic_load(&s_bt_fault) ? 2u : 0u) |
                     (bluetooth_service_is_running() ? 4u : 0u) | (competing_profiles() ? 8u : 0u) |
                     (spi_bridge_async_scan_busy() ? 16u : 0u) |
                     (session_manager_is_active() ? 32u : 0u) | (grok_wifi_is_active() ? 64u : 0u) |
                     (!ota_quiet() ? 128u : 0u);
  if (blocked) {
    bt_trace("busy_mask", (int)blocked);
    return false;
  }
  bool expected = false;
  if (!atomic_compare_exchange_strong(&s_bt_claimed, &expected, true)) {
    bt_trace("already_claimed", 1);
    return false;
  }
  int code = 0;
  const char *stage = "idle_deinit";
  /* Existing idle initialization can be rebuilt, never an active profile. */
  if (bluetooth_service_is_initialized() && (code = bluetooth_service_deinit()) != ESP_OK)
    goto failed;
  ble_hid_stack_reset();
  stage = "stack_reset";
  if (!hb_muse_setup_stack_reset()) {
    code = -1;
    goto failed;
  }
  stage = "nimble_init";
  bt_trace(stage, 0);
  if ((code = bluetooth_service_init()) != ESP_OK)
    goto failed;
  ble_svc_gap_init();
  ble_svc_gatt_init();
  stage = "device_name";
  if ((code = ble_svc_gap_device_name_set(s_name)) != 0)
    goto failed;
  stage = "gatt_register";
  if (!registration()) {
    code = -1;
    goto failed;
  }
  s_previous_reset = ble_hs_cfg.reset_cb;
  ble_hs_cfg.reset_cb = setup_reset;
  stage = "host_start";
  bt_trace(stage, 0);
  if ((code = bluetooth_service_start()) != ESP_OK)
    goto failed;
  bt_trace("host_ready", 0);
  return true;
failed:
  bt_trace(stage, code);
  release_ble(NULL);
  return false;
}
static bool advertise(void *u) {
  (void)u;
  if (!services_quiet(NULL) || !bluetooth_service_is_running()) {
    bt_trace("advertise_not_ready", 1);
    return false;
  }
  static ble_uuid128_t uuid = BLE_UUID128_INIT(0x0c,
                                               0x24,
                                               0x5f,
                                               0xcf,
                                               0x4e,
                                               0x31,
                                               0x46,
                                               0x8b,
                                               0xcf,
                                               0x46,
                                               0xea,
                                               0x38,
                                               0x1c,
                                               0x3d,
                                               0xdd,
                                               0x7f);
  struct ble_hs_adv_fields fields = {0}, scan = {0};
  static const uint8_t manufacturer[3] = {
      0xff, 0xff, 0x00}; /* pinned SDK: setup incomplete, not attestation */
  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
  fields.uuids128 = &uuid;
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;
  fields.mfg_data = manufacturer;
  fields.mfg_data_len = sizeof(manufacturer);
  scan.name = (uint8_t *)s_name;
  scan.name_len = (uint8_t)strlen(s_name);
  scan.name_is_complete = 1;
  struct ble_gap_adv_params params = {0};
  params.conn_mode = BLE_GAP_CONN_MODE_UND;
  params.disc_mode = BLE_GAP_DISC_MODE_GEN;
  int code = ble_gap_adv_set_fields(&fields);
  if (code) {
    bt_trace("advertise_fields", code);
    return false;
  }
  code = ble_gap_adv_rsp_set_fields(&scan);
  if (code) {
    bt_trace("advertise_response", code);
    return false;
  }
  code = ble_gap_adv_start(
      bluetooth_service_get_own_addr_type(), NULL, BLE_HS_FOREVER, &params, setup_gap, NULL);
  bt_trace("advertise_start", code);
  return code == 0;
}
static bool wifi_provision(
    void *u, const char *ssid, const char *password, bool (*guard)(void *), void *guard_user) {
  (void)u;
  if (!services_quiet(NULL) || !hb_muse_wifi_provision_guarded(ssid, password, guard, guard_user))
    return false;
  int64_t start = esp_timer_get_time(), last = start;
  if (start < 0)
    return false;
  for (;;) {
    int64_t now = esp_timer_get_time();
    if (now < last || now < start || now - start >= INT64_C(15000000) || !guard(guard_user))
      return false;
    last = now;
    wifi_ap_record_t ap = {0};
    if (wifi_service_is_connected() && esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
        strnlen((const char *)ap.ssid, sizeof(ap.ssid)) == strlen(ssid) &&
        memcmp(ap.ssid, ssid, strlen(ssid)) == 0 && guard(guard_user))
      return true;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
static bool wifi_scan(void *u,
                      hb_muse_pairing_network_t *out,
                      size_t cap,
                      size_t *n,
                      bool (*guard)(void *),
                      void *guard_user) {
  (void)u;
  return services_quiet(NULL) && hb_muse_wifi_scan_guarded(out, cap, n, guard, guard_user);
}
static bool initialize_setup(void) {
  if (s_setup_initialized)
    return true;
  if (s_setup_attempted || !ensure_initialized(NULL))
    return false;
  s_setup_attempted = true;
  hb_muse_setup_config_t config = {.now_ms = now_ms,
                                   .acquire_ble = acquire_ble,
                                   .advertise = advertise,
                                   .release_ble = release_ble,
                                   .services_quiet = services_quiet,
                                   .wifi_provision = wifi_provision,
                                   .node_id = s_node,
                                   .device_id = s_device,
                                   .mac = s_mac,
                                   .firmware_version = FIRMWARE_VERSION,
                                   .wifi_scan = wifi_scan};
  s_setup_initialized = hb_muse_setup_init(&config);
  return s_setup_initialized;
}
static bool token_quiet(void) {
  hb_muse_runtime_status_t runtime;
  hb_muse_setup_status_t setup;
  return s_setup_initialized && hb_muse_setup_status(&setup) && !setup.busy &&
         !setup.token_loaded && !atomic_load(&s_bt_claimed) && !atomic_load(&s_bt_fault) &&
         !competing_profiles() && !bluetooth_service_is_running() && !grok_wifi_is_active() &&
         !session_manager_is_active() && !spi_bridge_async_scan_busy() && ota_quiet() &&
         hb_muse_runtime_status(&runtime) && !runtime.enabled && !runtime.cloud_authorized &&
         !runtime.worker_started &&
         ((runtime.phase == HB_MUSE_RUNTIME_DISABLED && runtime.fenced) ||
          runtime.phase == HB_MUSE_RUNTIME_INITIALIZING);
}
/* UART may be armed while the inert owner initializes. Copying a token still
 * requires the real DISABLED fence, rechecked before the guarded short lock. */
static bool token_store(const char *token, size_t n, bool (*guard)(void *), void *user) {
  hb_muse_runtime_status_t runtime;
  return token_quiet() && hb_muse_runtime_status(&runtime) &&
         runtime.phase == HB_MUSE_RUNTIME_DISABLED && runtime.fenced && !runtime.enabled &&
         !runtime.cloud_authorized && !runtime.worker_started &&
         hb_muse_setup_set_local_token_guarded(token, n, guard, user);
}
static void put64(uint8_t *p, uint64_t value) {
  for (unsigned i = 0; i < 8; ++i)
    p[i] = (uint8_t)(value >> (8u * i));
}
static uint64_t get64(const uint8_t *p) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i)
    value |= (uint64_t)p[i] << (8u * i);
  return value;
}
static spi_status_t
setup_rpc(uint16_t cmd, const uint8_t *in, uint8_t n, uint8_t *out, uint8_t *written) {
  *written = 0;
  if (cmd == 0x1109 && (n != 17 || !in || in[16] > 1))
    return SPI_STATUS_INVALID_ARG;
  if (cmd != 0x1109 && n)
    return SPI_STATUS_INVALID_ARG;
  if (cmd == 0x1108) {
    hb_muse_setup_status_t s = {0};
    bool valid = hb_muse_setup_status(&s);
    memset(out, 0, 32);
    out[0] = 1;
    if (valid) {
      out[1] = (uint8_t)s.phase;
      out[2] = (uint8_t)s.error;
      out[3] = (s.token_loaded ? 1u : 0u) | (s.busy ? 2u : 0u) | (s.subscribed ? 4u : 0u) |
               (s.release_seen ? 8u : 0u) | (s.wifi_requested ? 16u : 0u) |
               (s.account_stored ? 32u : 0u);
      put64(out + 4, s.window);
      put64(out + 12, s.confirmation_ticket);
      for (unsigned i = 0; i < 4; ++i)
        out[20 + i] = (uint8_t)(s.dropped >> (8u * i));
    } else
      out[2] = HB_MUSE_SETUP_TOKEN_REQUIRED;
    *written = 32;
    return SPI_STATUS_OK;
  }
  bool accepted = false;
  if (cmd == 0x110a) {
    hb_muse_c5_service_revoke();
    accepted = true;
  } else if (cmd == 0x1109)
    accepted = hb_muse_setup_button(get64(in), get64(in + 8), in[16] != 0);
  else if (cmd == 0x1107 && !hb_muse_token_uart_active() && !atomic_load(&s_bt_fault) &&
           initialize_setup())
    accepted = hb_muse_setup_open();
  else if (cmd == 0x110b && initialize_setup())
    accepted = hb_muse_token_uart_arm(token_store, token_quiet);
  out[0] = accepted ? 1 : 0;
  *written = 1;
  return SPI_STATUS_OK;
}
bool hb_muse_c5_service_allows(uint16_t cmd) {
  bool reserved = hb_muse_setup_busy() || atomic_load(&s_bt_claimed) || hb_muse_token_uart_active();
  if (!reserved)
    return true;
  uint8_t category = (uint8_t)(cmd >> 8);
  if (cmd == SPI_ID_BT_GATT_STOP || cmd == SPI_ID_BT_GATT_STATUS)
    return true;
  if (category == SPI_CAT_MQTT)
    return cmd == SPI_ID_MQTT_STOP || cmd == SPI_ID_MQTT_STATUS;
  if (category == SPI_CAT_WIFI || category == SPI_CAT_BT || category == SPI_CAT_MESH ||
      category == SPI_CAT_MCORE || category == SPI_CAT_HOST || category == SPI_CAT_GROK)
    return false;
  return cmd != SPI_ID_SYSTEM_OTA_BEGIN && cmd != SPI_ID_SYSTEM_OTA_DATA;
}
void hb_muse_c5_service_register(void) {
  if (s_registered)
    return;
  const hb_muse_rpc_ops_t ops = {.ensure_initialized = ensure_initialized,
                                 .enable = enable,
                                 .connect = connect_cloud,
                                 .revoke = revoke,
                                 .status = status,
                                 .submit = submit,
                                 .take_reply = take_reply};
  s_registered = hb_muse_rpc_init(&s_rpc, &ops, NULL);
}
spi_status_t hb_muse_c5_service_execute(
    uint16_t cmd, const uint8_t *in, uint8_t n, uint8_t *out, uint8_t *written) {
  if (!written)
    return SPI_STATUS_INVALID_ARG;
  *written = 0;
  if (!s_registered)
    return SPI_STATUS_ERROR;
  if (cmd >= 0x1107 && cmd <= 0x110b)
    return setup_rpc(cmd, in, n, out, written);
  if ((hb_muse_setup_busy() || atomic_load(&s_bt_claimed) || hb_muse_token_uart_active()) &&
      (cmd == HB_MUSE_RPC_ENABLE || cmd == HB_MUSE_RPC_CONNECT || cmd == HB_MUSE_RPC_TX))
    return SPI_STATUS_BUSY;
  if (cmd == HB_MUSE_RPC_REVOKE) {
    hb_muse_setup_revoke();
    hb_muse_token_uart_cancel();
  }
  size_t bytes = 0;
  hb_muse_rpc_result_t result =
      hb_muse_rpc_execute(&s_rpc, cmd, in, n, out, SPI_MAX_PAYLOAD - SPI_RESP_STATUS_SIZE, &bytes);
  if (bytes > SPI_MAX_PAYLOAD - SPI_RESP_STATUS_SIZE)
    return SPI_STATUS_ERROR;
  *written = (uint8_t)bytes;
  return (spi_status_t)result;
}
void hb_muse_c5_service_revoke(void) {
  hb_muse_setup_revoke();
  hb_muse_token_uart_cancel();
  if (atomic_load(&s_initialized))
    hb_muse_runtime_revoke();
}
