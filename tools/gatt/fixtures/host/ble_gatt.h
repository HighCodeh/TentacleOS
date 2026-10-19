#pragma once
#include <stdint.h>
#define BLE_UUID_TYPE_16 16
#define BLE_UUID_TYPE_32 32
#define BLE_UUID_TYPE_128 128
#define BLE_HS_CONN_HANDLE_NONE 0xffff
#define BLE_ADDR_RANDOM_ID 3
#define BLE_ERR_REM_USER_CONN_TERM 19
#define BLE_HS_EDONE 14
#define BLE_HS_ENOTCONN 7
#define BLE_HS_EALREADY 2
#define BLE_HS_ETIMEOUT 13
#define BLE_HS_EINVAL 3
#define BLE_HS_EMSGSIZE 4
#define BLE_HS_EBADDATA 10
#define BLE_HS_EOS 11
typedef union {
 struct {uint8_t type;} u;
 struct {uint8_t type; uint16_t value;} u16;
 struct {uint8_t type; uint32_t value;} u32;
 struct {uint8_t type; uint8_t value[16];} u128;
} ble_uuid_any_t;
struct ble_gatt_error {int status;};
struct ble_gatt_svc {uint16_t start_handle,end_handle;ble_uuid_any_t uuid;};
struct ble_gatt_chr {uint16_t val_handle;uint8_t properties;ble_uuid_any_t uuid;};
struct os_mbuf;
struct ble_gatt_attr {uint16_t handle;struct os_mbuf *om;};
typedef int (*gatt_svc_cb_t)(uint16_t,const struct ble_gatt_error *,const struct ble_gatt_svc *,void *);
typedef int (*gatt_chr_cb_t)(uint16_t,const struct ble_gatt_error *,const struct ble_gatt_chr *,void *);
typedef int (*gatt_read_cb_t)(uint16_t,const struct ble_gatt_error *,struct ble_gatt_attr *,void *);
int ble_gattc_disc_all_svcs(uint16_t,gatt_svc_cb_t,void *);
int ble_gattc_disc_all_chrs(uint16_t,uint16_t,uint16_t,gatt_chr_cb_t,void *);
int ble_gattc_read(uint16_t,uint16_t,gatt_read_cb_t,void *);
