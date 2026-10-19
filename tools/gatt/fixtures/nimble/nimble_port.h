#pragma once
#include <stdint.h>
struct ble_npl_event {void (*callback)(struct ble_npl_event *);};
struct ble_npl_callout {struct ble_npl_event event;};
struct ble_npl_eventq {int unused;};
void ble_npl_event_init(struct ble_npl_event *,void (*)(struct ble_npl_event *),void *);
void ble_npl_eventq_put(struct ble_npl_eventq *,struct ble_npl_event *);
int ble_npl_callout_init(struct ble_npl_callout *,struct ble_npl_eventq *,void (*)(struct ble_npl_event *),void *);
int ble_npl_callout_reset(struct ble_npl_callout *,uint32_t);
void ble_npl_callout_stop(struct ble_npl_callout *);
uint32_t ble_npl_time_ms_to_ticks32(uint32_t);
struct ble_npl_eventq *nimble_port_get_dflt_eventq(void);

void ble_npl_event_deinit(struct ble_npl_event *);
void ble_npl_eventq_remove(struct ble_npl_eventq *,struct ble_npl_event *);
void ble_npl_callout_deinit(struct ble_npl_callout *);
