#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef int ble_npl_event_fn;
typedef int ble_npl_event;
typedef int ble_npl_callout;
typedef int ble_npl_mutex;
typedef int ble_npl_sem;
typedef int ble_addr_t;
typedef int ble_uuid_any_t;
typedef int ble_gap_event;
typedef int os_mempool;
typedef int os_mbuf;

#define MYNEWT_VAL(x) x

#ifdef __cplusplus
}
#endif
