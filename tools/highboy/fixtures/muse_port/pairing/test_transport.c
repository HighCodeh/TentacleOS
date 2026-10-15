/* Real passive registration and fragment codec; explicit NimBLE fakes only.
 * No radio, host task, account credentials or encryption runs here. */
#include "hb_muse_pairing_fragments.h"
#include "hb_muse_pairing_gatt.h"
#include "host/ble_gatt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)
static const struct ble_gatt_svc_def *services;
static bool link_current = true, queue_room = true, notify_ok = true;
static unsigned deferred, notifications, registration_calls;
static uint8_t deferred_copy[512];
int ble_gatts_count_cfg(const struct ble_gatt_svc_def *defs) { ++registration_calls; services = defs; return 0; }
int ble_gatts_add_svcs(const struct ble_gatt_svc_def *defs) {
    CHECK(defs == services); ++registration_calls;
    *defs[0].characteristics[0].val_handle = 10; *defs[0].characteristics[1].val_handle = 11; return 0;
}
int ble_hs_mbuf_to_flat(const struct os_mbuf *m, void *out, uint16_t cap, uint16_t *copied) {
    CHECK(cap >= m->bytes); memcpy(out, m->data, m->bytes); *copied = m->bytes; return 0;
}
struct os_mbuf *ble_hs_mbuf_from_flat(const void *data, uint16_t bytes) {
    struct os_mbuf *m = calloc(1, sizeof(*m)); CHECK(m && bytes <= sizeof(m->data));
    memcpy(m->data, data, bytes); m->bytes = bytes; return m;
}
int os_mbuf_append(struct os_mbuf *m, const void *data, uint16_t bytes) {
    CHECK((size_t)m->bytes + bytes <= sizeof(m->data)); memcpy(m->data + m->bytes, data, bytes);
    m->bytes = (uint16_t)(m->bytes + bytes); return 0;
}
int ble_gatts_notify_custom(uint16_t connection, uint16_t handle, struct os_mbuf *m) {
    CHECK(connection == 7 && handle == 11); ++notifications; free(m); return notify_ok ? 0 : -1;
}
static bool current(void *user, uint16_t connection) { (void)user; return link_current && connection == 7; }
static bool enqueue(void *user, uint16_t connection, const uint8_t *data, size_t bytes) {
    (void)user; CHECK(connection == 7 && bytes <= sizeof(deferred_copy));
    if (!queue_room) return false;
    memcpy(deferred_copy, data, bytes); ++deferred; return true;
}
int main(void) {
    unsigned groups = 0;
    CHECK(!hb_muse_pairing_gatt_registered() && !hb_muse_pairing_gatt_notify(7, (const uint8_t *)"x", 1));
    hb_muse_pairing_gatt_config_t cfg = {current, enqueue, NULL};
    CHECK(hb_muse_pairing_gatt_register(&cfg)); CHECK(!hb_muse_pairing_gatt_register(&cfg));
    CHECK(registration_calls == 2 && hb_muse_pairing_gatt_tx_handle() == 11);
    const ble_uuid128_t *uuid = (const ble_uuid128_t *)services[0].uuid;
    const uint8_t expected[16] = {0x0c,0x24,0x5f,0xcf,0x4e,0x31,0x46,0x8b,0xcf,0x46,0xea,0x38,0x1c,0x3d,0xdd,0x7f};
    CHECK(!memcmp(uuid->value, expected, 16)); ++groups;
    struct os_mbuf message = {0}; memcpy(message.data, "{}", 2); message.bytes = 2;
    struct ble_gatt_access_ctxt ctxt = {BLE_GATT_ACCESS_OP_WRITE_CHR, &message};
    const struct ble_gatt_chr_def *rx = &services[0].characteristics[0];
    CHECK(rx->access_cb(7, 10, &ctxt, NULL) == 0 && deferred == 1 && !memcmp(deferred_copy, "{}", 2));
    queue_room = false; CHECK(rx->access_cb(7, 10, &ctxt, NULL) == BLE_ATT_ERR_INSUFFICIENT_RES);
    link_current = false; CHECK(rx->access_cb(7, 10, &ctxt, NULL) == BLE_ATT_ERR_UNLIKELY && deferred == 1);
    link_current = true; message.bytes = 0; CHECK(rx->access_cb(7, 10, &ctxt, NULL) != 0); ++groups;
    CHECK(hb_muse_pairing_gatt_notify(7, (const uint8_t *)"fixture", 7));
    notify_ok = false; CHECK(!hb_muse_pairing_gatt_notify(7, (const uint8_t *)"fixture", 7));
    CHECK(notifications == 2); link_current = false;
    CHECK(!hb_muse_pairing_gatt_notify(7, (const uint8_t *)"fixture", 7) && notifications == 2); ++groups;

    hb_muse_pairing_fragments_t state = {0}; uint8_t *out = NULL; size_t bytes = 0;
    CHECK(hb_muse_pairing_fragments_feed(&state, 1, (const uint8_t *)"{}", 2, 0, &out, &bytes) == HB_MUSE_FRAGMENT_READY);
    CHECK(bytes == 2 && !strcmp((char *)out, "{}")); hb_muse_pairing_fragments_release(out, bytes); ++groups;
    uint8_t first[] = {0xfe, 0, 2, '{'}, last[] = {0xfe, 1, 2, '}'};
    CHECK(hb_muse_pairing_fragments_feed(&state, 1, first, sizeof(first), 1, &out, &bytes) == HB_MUSE_FRAGMENT_MORE);
    CHECK(!out && !bytes);
    CHECK(hb_muse_pairing_fragments_feed(&state, 1, last, sizeof(last), 2, &out, &bytes) == HB_MUSE_FRAGMENT_READY);
    CHECK(bytes == 2 && !strcmp((char *)out, "{}")); hb_muse_pairing_fragments_release(out, bytes); ++groups;
    CHECK(hb_muse_pairing_fragments_feed(&state, 1, last, sizeof(last), 3, &out, &bytes) == HB_MUSE_FRAGMENT_BAD);
    CHECK(hb_muse_pairing_fragments_feed(&state, 1, first, sizeof(first), 4, &out, &bytes) == HB_MUSE_FRAGMENT_MORE);
    CHECK(hb_muse_pairing_fragments_feed(&state, 2, last, sizeof(last), 5, &out, &bytes) == HB_MUSE_FRAGMENT_BAD); ++groups;
    CHECK(hb_muse_pairing_fragments_feed(&state, 2, first, sizeof(first), 6, &out, &bytes) == HB_MUSE_FRAGMENT_MORE);
    CHECK(hb_muse_pairing_fragments_feed(&state, 2, last, sizeof(last), 5006, &out, &bytes) == HB_MUSE_FRAGMENT_BAD); ++groups;
    CHECK(hb_muse_pairing_fragments_feed(&state, 3, first, sizeof(first), 5007, &out, &bytes) == HB_MUSE_FRAGMENT_MORE);
    CHECK(hb_muse_pairing_fragments_feed(&state, 3, last, sizeof(last), 5006, &out, &bytes) == HB_MUSE_FRAGMENT_BAD);
    CHECK(state.clock_fault && !state.buffer); ++groups;

    state = (hb_muse_pairing_fragments_t){0};
    uint8_t input[8192], fragment[160], total = 0; size_t fragment_bytes = 0;
    memset(input, 'X', sizeof(input));
    CHECK(!hb_muse_pairing_fragment_encode(input, sizeof(input), 23, 0, fragment, &fragment_bytes, &total));
    CHECK(!hb_muse_pairing_fragment_encode(input, sizeof(input), 6, 0, fragment, &fragment_bytes, &total)); ++groups;
    uint64_t now = 1;
    for (uint8_t index = 0;; ++index) {
        CHECK(hb_muse_pairing_fragment_encode(input, sizeof(input), 247, index, fragment, &fragment_bytes, &total));
        CHECK(fragment_bytes <= 160);
        hb_muse_fragment_result_t r = hb_muse_pairing_fragments_feed(&state, 5, fragment, fragment_bytes, now++, &out, &bytes);
        if ((unsigned)index + 1u == total) { CHECK(r == HB_MUSE_FRAGMENT_READY); break; }
        CHECK(r == HB_MUSE_FRAGMENT_MORE);
    }
    CHECK(bytes == sizeof(input) && !memcmp(input, out, bytes)); hb_muse_pairing_fragments_release(out, bytes); ++groups;
    CHECK(!hb_muse_pairing_fragment_encode(input, sizeof(input) + 1u, 247, 0, fragment, &fragment_bytes, &total));
    uint8_t overflow[512]; memset(overflow, 'X', sizeof(overflow)); overflow[0] = 0xfe; overflow[2] = 17;
    for (uint8_t index = 0; index < 17; ++index) {
        overflow[1] = index;
        hb_muse_fragment_result_t r = hb_muse_pairing_fragments_feed(&state, 6, overflow, sizeof(overflow), now++, &out, &bytes);
        CHECK(r == (index == 16 ? HB_MUSE_FRAGMENT_BAD : HB_MUSE_FRAGMENT_MORE));
    }
    CHECK(!out && !bytes && !state.buffer); ++groups;
    hb_muse_pairing_fragments_drop(&state);
    printf("PASS %u passive-GATT/fragment groups (explicit NimBLE fakes, no host or radio)\n", groups);
    return 0;
}
