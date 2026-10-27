#ifndef HB_PAIRING_FAKE_NIMBLE_H
#define HB_PAIRING_FAKE_NIMBLE_H
/* Explicit host fixture ABI subset, not a NimBLE implementation. */
#include <stdint.h>
#include <stddef.h>
typedef struct { uint8_t type; } ble_uuid_t;
typedef struct { ble_uuid_t u; uint8_t value[16]; } ble_uuid128_t;
#define BLE_UUID128_INIT(...) {{128}, {__VA_ARGS__}}
struct os_mbuf { uint16_t bytes; uint8_t data[512]; };
#define OS_MBUF_PKTLEN(m) ((m)->bytes)
struct ble_gatt_access_ctxt { uint8_t op; struct os_mbuf *om; };
struct ble_gatt_chr_def {
    const ble_uuid_t *uuid;
    int (*access_cb)(uint16_t, uint16_t, struct ble_gatt_access_ctxt *, void *);
    uint16_t flags;
    uint16_t *val_handle;
};
struct ble_gatt_svc_def { uint8_t type; const ble_uuid_t *uuid; const struct ble_gatt_chr_def *characteristics; };
#define BLE_GATT_ACCESS_OP_WRITE_CHR 1
#define BLE_GATT_ACCESS_OP_READ_CHR 2
#define BLE_GATT_CHR_F_WRITE 1
#define BLE_GATT_CHR_F_WRITE_NO_RSP 2
#define BLE_GATT_CHR_F_READ 4
#define BLE_GATT_CHR_F_NOTIFY 8
#define BLE_GATT_SVC_TYPE_PRIMARY 1
#define BLE_ATT_ERR_UNLIKELY 14
#define BLE_ATT_ERR_INSUFFICIENT_RES 17
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 13
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *);
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *);
int ble_hs_mbuf_to_flat(const struct os_mbuf *, void *, uint16_t, uint16_t *);
struct os_mbuf *ble_hs_mbuf_from_flat(const void *, uint16_t);
int os_mbuf_append(struct os_mbuf *, const void *, uint16_t);
int ble_gatts_notify_custom(uint16_t, uint16_t, struct os_mbuf *);
#endif
