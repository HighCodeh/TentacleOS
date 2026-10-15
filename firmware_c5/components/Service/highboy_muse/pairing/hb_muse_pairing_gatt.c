#include "hb_muse_pairing_gatt.h"
#include "hb_muse_pairing_fragments.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include <stdatomic.h>
#include <string.h>

/* Pinned Muse community-v5 UUIDs, little-endian NimBLE byte order. */
static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(
    0x0c, 0x24, 0x5f, 0xcf, 0x4e, 0x31, 0x46, 0x8b, 0xcf, 0x46, 0xea, 0x38, 0x1c, 0x3d, 0xdd, 0x7f);
static const ble_uuid128_t s_rx_uuid = BLE_UUID128_INIT(
    0x01, 0x9b, 0x8f, 0x5e, 0x2d, 0x3c, 0xf0, 0xa1, 0x6e, 0x4a, 0xa2, 0x28, 0x29, 0x30, 0x59, 0x4d);
static const ble_uuid128_t s_tx_uuid = BLE_UUID128_INIT(
    0x6c, 0x5b, 0x4a, 0x3f, 0x2e, 0x1d, 0x0a, 0x8f, 0x9c, 0x4e, 0x2b, 0x7b, 0xca, 0xc4, 0x5d, 0xd7);
static hb_muse_pairing_gatt_config_t s_config;
static atomic_bool s_attempted, s_registered;
static uint16_t s_rx_handle, s_tx_handle;
static int
rx_access(uint16_t connection, uint16_t attribute, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)attribute;
  (void)arg;
  if (!atomic_load(&s_registered) || !ctxt || ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR ||
      !s_config.current(s_config.user, connection))
    return BLE_ATT_ERR_UNLIKELY;
  uint16_t bytes = OS_MBUF_PKTLEN(ctxt->om);
  if (!bytes || bytes > HB_MUSE_PAIRING_ATT_MAX)
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  uint8_t fragment[HB_MUSE_PAIRING_ATT_MAX];
  uint16_t copied = 0;
  int result = ble_hs_mbuf_to_flat(ctxt->om, fragment, bytes, &copied);
  bool accepted = result == 0 && copied == bytes && s_config.current(s_config.user, connection) &&
                  s_config.defer_rx(s_config.user, connection, fragment, bytes);
  volatile uint8_t *p = fragment;
  for (size_t i = 0; i < sizeof(fragment); ++i)
    p[i] = 0;
  return accepted ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}
static int
tx_access(uint16_t connection, uint16_t attribute, struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)attribute;
  (void)arg;
  if (!atomic_load(&s_registered) || !ctxt || ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR ||
      !s_config.current(s_config.user, connection))
    return BLE_ATT_ERR_UNLIKELY;
  /* Generic idle value is not an account/cloud pairing claim. */
  return os_mbuf_append(ctxt->om, "idle", 4) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}
static const struct ble_gatt_chr_def s_characteristics[] = {
    {.uuid = &s_rx_uuid.u,
     .access_cb = rx_access,
     .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
     .val_handle = &s_rx_handle},
    {.uuid = &s_tx_uuid.u,
     .access_cb = tx_access,
     .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
     .val_handle = &s_tx_handle},
    {0}};
static const struct ble_gatt_svc_def s_services[] = {{.type = BLE_GATT_SVC_TYPE_PRIMARY,
                                                      .uuid = &s_service_uuid.u,
                                                      .characteristics = s_characteristics},
                                                     {0}};
bool hb_muse_pairing_gatt_register(const hb_muse_pairing_gatt_config_t *config) {
  if (!config || !config->current || !config->defer_rx || atomic_exchange(&s_attempted, true))
    return false;
  s_config = *config;
  bool ok = ble_gatts_count_cfg(s_services) == 0 && ble_gatts_add_svcs(s_services) == 0;
  atomic_store(&s_registered, ok);
  return ok;
}
bool hb_muse_pairing_gatt_registered(void) {
  return atomic_load(&s_registered);
}
uint16_t hb_muse_pairing_gatt_tx_handle(void) {
  return atomic_load(&s_registered) ? s_tx_handle : 0;
}
void hb_muse_pairing_gatt_stack_reset(void) {
  /* Caller proved host is stopped: no concurrent access callbacks exist. */
  atomic_store(&s_registered, false);
  memset(&s_config, 0, sizeof(s_config));
  s_rx_handle = s_tx_handle = 0;
  atomic_store(&s_attempted, false);
}
bool hb_muse_pairing_gatt_notify(uint16_t connection, const uint8_t *fragment, size_t bytes) {
  if (!atomic_load(&s_registered) || !s_tx_handle || !fragment || !bytes ||
      bytes > HB_MUSE_PAIRING_NOTIFY_MAX || !s_config.current(s_config.user, connection))
    return false;
  struct os_mbuf *packet = ble_hs_mbuf_from_flat(fragment, (uint16_t)bytes);
  if (!packet)
    return false;
  /* NimBLE consumes packet on both success and error. */
  return ble_gatts_notify_custom(connection, s_tx_handle, packet) == 0 &&
         s_config.current(s_config.user, connection);
}
