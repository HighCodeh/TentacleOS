/* Real codec/server with an explicit fake application. No SPI/SDK/network. */
#include "hb_muse_wire.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned can_open_calls, dispatch_calls, revoke_calls, opened_calls;
    uint32_t opened_epoch;
    bool fenced;
    hb_muse_wire_status_t dispatch_status;
    uint16_t reply_len;
    uint8_t reply[HB_MUSE_WIRE_PAYLOAD_MAX];
    hb_muse_wire_frame_t last_request;
} fake_t;
static bool can_open(void *user) {
    fake_t *f = user; ++f->can_open_calls; return f->fenced;
}
static hb_muse_wire_status_t dispatch(void *user, const hb_muse_wire_frame_t *request,
                                     uint8_t *reply, uint16_t *reply_len) {
    fake_t *f = user; ++f->dispatch_calls; f->last_request = *request;
    if (f->reply_len <= HB_MUSE_WIRE_PAYLOAD_MAX) memcpy(reply, f->reply, f->reply_len);
    *reply_len = f->reply_len; return f->dispatch_status;
}
static void revoke(void *user) {
    fake_t *f = user; ++f->revoke_calls; f->fenced = false;
}
static void opened(void *user, uint32_t epoch) {
    fake_t *f = user; ++f->opened_calls; f->opened_epoch = epoch;
}
static const hb_muse_wire_callbacks_t CALLBACKS = {can_open, dispatch, revoke, opened};
static const uint32_t BOOT = UINT32_C(0x88776655);
static const uint8_t NONCE[8] = {0x91,0x82,0x73,0x64,0x55,0x46,0x37,0x28};
typedef struct { hb_muse_wire_server_t server; fake_t fake; } fixture_t;
static void init(fixture_t *f, uint32_t lease) {
    memset(f, 0, sizeof(*f)); f->fake.fenced = true;
    assert(hb_muse_wire_server_init(&f->server, BOOT, lease, &CALLBACKS, &f->fake));
}
static hb_muse_wire_frame_t request(uint8_t opcode, uint32_t epoch, uint32_t sequence) {
    hb_muse_wire_frame_t frame = {0};
    frame.opcode = opcode; frame.boot = BOOT; frame.epoch = epoch; frame.sequence = sequence;
    if (opcode == HB_MUSE_WIRE_OPEN) {
        frame.payload_len = HB_MUSE_WIRE_NONCE_BYTES; memcpy(frame.payload, NONCE, sizeof(NONCE));
    }
    return frame;
}
static hb_muse_wire_frame_t hello(void) {
    hb_muse_wire_frame_t frame = {0}; frame.opcode = HB_MUSE_WIRE_HELLO;
    frame.payload_len = HB_MUSE_WIRE_NONCE_BYTES; memcpy(frame.payload, NONCE, sizeof(NONCE));
    return frame;
}
static size_t encode(const hb_muse_wire_frame_t *frame, uint8_t *bytes) {
    size_t n = 0; assert(hb_muse_wire_encode(frame, bytes, HB_MUSE_WIRE_FRAME_MAX, &n)); return n;
}
static hb_muse_wire_frame_t send(fixture_t *f, hb_muse_wire_frame_t frame, uint64_t now) {
    uint8_t input[HB_MUSE_WIRE_FRAME_MAX], output[HB_MUSE_WIRE_FRAME_MAX];
    size_t in_len = encode(&frame, input), out_len = 0;
    assert(hb_muse_wire_server_process(&f->server, input, in_len, now, output, sizeof(output), &out_len));
    hb_muse_wire_frame_t out;
    assert(hb_muse_wire_decode(output, out_len, &out));
    assert(out.flags == HB_MUSE_WIRE_RESPONSE && out.opcode == frame.opcode);
    assert(out.boot == BOOT && out.epoch == frame.epoch && out.sequence == frame.sequence);
    assert(out.generation == frame.generation && out.turn == frame.turn);
    return out;
}
static void open_at(fixture_t *f, uint32_t epoch, uint64_t now) {
    assert(send(f, request(HB_MUSE_WIRE_OPEN, epoch, 1), now).status == HB_MUSE_WIRE_OK);
    assert(f->server.active && f->server.max_epoch == epoch && f->server.last_sequence == 1);
}
static void put_crc(uint8_t *bytes, size_t length) {
    uint32_t crc = hb_muse_wire_crc32(bytes, length - 4);
    for (unsigned i = 0; i < 4; ++i) bytes[length - 4 + i] = (uint8_t)(crc >> (8u * i));
}
static void test_crc_and_exact_header(void) {
    const uint8_t vector[] = "123456789";
    assert(hb_muse_wire_crc32(vector, 9) == UINT32_C(0xcbf43926));
    assert(hb_muse_wire_crc32(NULL, 0) == 0);
    hb_muse_wire_frame_t f = request(HB_MUSE_WIRE_PCM, UINT32_C(0x10203040), UINT32_C(0x50607080));
    f.generation = UINT32_C(0x90a0b0c0); f.turn = UINT32_C(0xd0e0f001);
    f.payload_len = 3; memcpy(f.payload, "abc", 3);
    const uint8_t header[28] = {0x48,0x4d,1,4,0x55,0x66,0x77,0x88,0x40,0x30,0x20,0x10,
        0x80,0x70,0x60,0x50,0xc0,0xb0,0xa0,0x90,1,0xf0,0xe0,0xd0,3,0,0,0};
    uint8_t bytes[248]; size_t n = encode(&f, bytes);
    assert(n == 35 && !memcmp(bytes, header, sizeof(header)) && !memcmp(bytes + 28, "abc", 3));
    hb_muse_wire_frame_t decoded;
    assert(hb_muse_wire_decode(bytes, n, &decoded));
    assert(decoded.boot == f.boot && decoded.generation == f.generation && decoded.turn == f.turn);
    assert(decoded.payload_len == 3 && !memcmp(decoded.payload, "abc", 3));
}
static void test_codec_bounds_and_no_partial_write(void) {
    hb_muse_wire_frame_t f = request(HB_MUSE_WIRE_TEXT, 1, 2);
    f.flags = HB_MUSE_WIRE_RESPONSE; f.status = HB_MUSE_WIRE_BUSY; f.payload_len = 216;
    for (unsigned i = 0; i < 216; ++i) f.payload[i] = (uint8_t)i;
    uint8_t bytes[249]; memset(bytes, 0xaa, sizeof(bytes));
    size_t n = 777;
    assert(!hb_muse_wire_encode(&f, bytes, 247, &n) && n == 0 && bytes[0] == 0xaa);
    assert(encode(&f, bytes) == 248 && bytes[248] == 0xaa);
    hb_muse_wire_frame_t decoded;
    assert(hb_muse_wire_decode(bytes, 248, &decoded) && decoded.payload_len == 216);
    assert(!memcmp(decoded.payload, f.payload, 216));
    f.payload_len = 217; assert(!hb_muse_wire_encode(&f, bytes, 248, &n));
    f.payload_len = 0; f.flags = 2; assert(!hb_muse_wire_encode(&f, bytes, 248, &n));
    f.flags = 0; assert(!hb_muse_wire_encode(&f, bytes, 248, &n)); /* request status !=0 */
    f.status = 0; f.opcode = 11; assert(!hb_muse_wire_encode(&f, bytes, 248, &n));
    f.opcode = 0; assert(!hb_muse_wire_encode(&f, bytes, 248, &n));
    assert(!hb_muse_wire_encode(NULL, bytes, 248, &n));
    assert(!hb_muse_wire_encode(&f, NULL, 248, &n));
    assert(!hb_muse_wire_encode(&f, bytes, 248, NULL));
}
static void test_corruption_and_malformed_decode_preserve_output(void) {
    hb_muse_wire_frame_t f = request(HB_MUSE_WIRE_PCM, 1, 2);
    f.payload_len = 216; memset(f.payload, 0x7b, sizeof(f.payload));
    uint8_t original[249], bytes[249]; const size_t n = encode(&f, original);
    hb_muse_wire_frame_t out, before; memset(&before, 0x96, sizeof(before));
    for (size_t i = 0; i < n; ++i) {
        memcpy(bytes, original, n); bytes[i] ^= 1; out = before;
        assert(!hb_muse_wire_decode(bytes, n, &out)); assert(!memcmp(&out, &before, sizeof(out)));
    }
    for (size_t length = 0; length < n; ++length) {
        out = before; assert(!hb_muse_wire_decode(original, length, &out));
        assert(!memcmp(&out, &before, sizeof(out)));
    }
    assert(!hb_muse_wire_decode(original, n + 1, &out));
    assert(!hb_muse_wire_decode(NULL, n, &out) && !hb_muse_wire_decode(original, n, NULL));
    const unsigned offsets[] = {0, 2, 3, 24, 26, 27};
    const uint8_t bad_values[] = {0, 2, 11, 217, 1, 2};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        memcpy(bytes, original, n); bytes[offsets[i]] = bad_values[i]; put_crc(bytes, n);
        out = before; assert(!hb_muse_wire_decode(bytes, n, &out));
        assert(!memcmp(&out, &before, sizeof(out)));
    }
}
static void test_init_validation(void) {
    fixture_t f; init(&f, 0); assert(f.server.lease_ms == 2000);
    assert(!hb_muse_wire_server_init(NULL, BOOT, 0, &CALLBACKS, NULL));
    assert(!hb_muse_wire_server_init(&f.server, 0, 0, &CALLBACKS, NULL));
    assert(!f.server.initialized);
    assert(!hb_muse_wire_server_init(&f.server, BOOT, 0, NULL, NULL));
    hb_muse_wire_callbacks_t bad = CALLBACKS; bad.revoke = NULL;
    assert(!hb_muse_wire_server_init(&f.server, BOOT, 0, &bad, NULL));
    hb_muse_wire_server_expire(NULL, 1);
}
static void test_hello_and_open_guard(void) {
    fixture_t f; init(&f, 0);
    hb_muse_wire_frame_t out = send(&f, hello(), 100);
    assert(out.status == HB_MUSE_WIRE_OK && out.payload_len == 13);
    const uint8_t zero[5] = {0}; assert(!memcmp(out.payload, zero, 5));
    assert(!memcmp(out.payload + 5, NONCE, sizeof(NONCE)));
    assert(!f.fake.can_open_calls && !f.fake.dispatch_calls && !f.fake.revoke_calls);
    f.fake.fenced = false;
    assert(send(&f, request(HB_MUSE_WIRE_OPEN, 1, 1), 101).status == HB_MUSE_WIRE_BUSY);
    assert(!f.server.max_epoch && !f.server.active && !f.server.cache_valid);
    f.fake.fenced = true; open_at(&f, 5, 102);
    out = send(&f, hello(), 103);
    assert(out.payload[0] == 5 && out.payload[4] == 1 && f.server.last_activity_ms == 102);
    assert(!f.fake.dispatch_calls);
}
static void test_invalid_control_and_response_requests(void) {
    fixture_t f; init(&f, 0); hb_muse_wire_frame_t h = hello(); h.sequence = 1;
    assert(send(&f, h, 100).status == HB_MUSE_WIRE_REJECTED);
    assert(!f.server.clock_started && !f.fake.can_open_calls);
    const uint32_t epochs[] = {0, UINT32_MAX};
    for (unsigned i = 0; i < 2; ++i)
        assert(send(&f, request(HB_MUSE_WIRE_OPEN, epochs[i], 1), 100).status == HB_MUSE_WIRE_REJECTED);
    hb_muse_wire_frame_t o = request(HB_MUSE_WIRE_OPEN, 1, 2);
    assert(send(&f, o, 100).status == HB_MUSE_WIRE_REJECTED);
    o.sequence = 1; o.generation = 1;
    assert(send(&f, o, 100).status == HB_MUSE_WIRE_REJECTED);
    o.generation = 0; o.payload_len = 1;
    assert(send(&f, o, 100).status == HB_MUSE_WIRE_REJECTED);
    uint8_t in[248], out[248]; size_t out_n = 9;
    o.payload_len = HB_MUSE_WIRE_NONCE_BYTES; o.flags = 1; size_t in_n = encode(&o, in);
    assert(!hb_muse_wire_server_process(&f.server, in, in_n, 100, out, 248, &out_n));
    assert(!out_n && !f.fake.can_open_calls && !f.server.max_epoch);
}
static void test_small_response_buffer_and_bad_crc_no_mutation(void) {
    fixture_t f; init(&f, 0); open_at(&f, 1, 100);
    uint8_t in[248], out[248]; hb_muse_wire_frame_t req = request(HB_MUSE_WIRE_BEGIN, 1, 2);
    size_t n = encode(&req, in), out_n = 999;
    hb_muse_wire_server_t before = f.server;
    assert(!hb_muse_wire_server_process(&f.server, in, n, 3000, out, 247, &out_n));
    assert(out_n == 0 && !memcmp(&before, &f.server, sizeof(before)));
    in[n - 1] ^= 1;
    assert(!hb_muse_wire_server_process(&f.server, in, n, 3000, out, 248, &out_n));
    assert(!memcmp(&before, &f.server, sizeof(before)) && !f.fake.dispatch_calls && !f.fake.revoke_calls);
}
static void test_response_loss_exact_replay_no_side_effect(void) {
    fixture_t f; init(&f, 0); open_at(&f, 1, 100);
    hb_muse_wire_frame_t req = request(HB_MUSE_WIRE_PCM, 1, 2);
    req.generation = 3; req.turn = 7; req.payload_len = 4; memcpy(req.payload, "PCM!", 4);
    f.fake.reply_len = 3; memcpy(f.fake.reply, "yes", 3);
    uint8_t in[248], first[248], second[248]; size_t in_n = encode(&req, in), n1, n2;
    assert(hb_muse_wire_server_process(&f.server, in, in_n, 200, first, 248, &n1));
    assert(f.fake.dispatch_calls == 1 && f.server.last_activity_ms == 200);
    memset(f.fake.reply, 0xab, sizeof(f.fake.reply));
    assert(hb_muse_wire_server_process(&f.server, in, in_n, 1999, second, 248, &n2));
    assert(n1 == n2 && !memcmp(first, second, n1));
    assert(f.fake.dispatch_calls == 1 && f.server.last_activity_ms == 200);
    assert(f.fake.last_request.generation == 3 && f.fake.last_request.turn == 7);
    assert(!memcmp(f.fake.last_request.payload, "PCM!", 4));
}
static void test_duplicate_open_and_alias_input_output(void) {
    fixture_t f; init(&f, 0); hb_muse_wire_frame_t req = request(HB_MUSE_WIRE_OPEN, 1, 1);
    uint8_t bytes[248], original[248]; size_t n = encode(&req, bytes), out_n;
    memcpy(original, bytes, n);
    assert(hb_muse_wire_server_process(&f.server, bytes, n, 100, bytes, 248, &out_n));
    assert(f.fake.can_open_calls == 1);
    uint8_t duplicate[248]; size_t duplicate_n;
    assert(hb_muse_wire_server_process(&f.server, original, n, 101, duplicate, 248, &duplicate_n));
    assert(duplicate_n == out_n && !memcmp(bytes, duplicate, out_n));
    assert(f.fake.can_open_calls == 1 && f.server.last_activity_ms == 100);
}
static void test_conflicts_and_gaps(void) {
    fixture_t f; init(&f, 0); open_at(&f, 1, 100);
    hb_muse_wire_frame_t req = request(HB_MUSE_WIRE_PCM, 1, 2); req.payload_len = 1; req.payload[0] = 3;
    assert(send(&f, req, 101).status == HB_MUSE_WIRE_OK);
    req.payload[0] = 4; assert(send(&f, req, 102).status == HB_MUSE_WIRE_CONFLICT);
    req.payload[0] = 3; req.generation = 1; assert(send(&f, req, 102).status == HB_MUSE_WIRE_CONFLICT);
    req.generation = 0; req.opcode = HB_MUSE_WIRE_END;
    assert(send(&f, req, 102).status == HB_MUSE_WIRE_CONFLICT);
    req.sequence = 4; assert(send(&f, req, 102).status == HB_MUSE_WIRE_GAP);
    req.sequence = 0; assert(send(&f, req, 102).status == HB_MUSE_WIRE_STALE);
    req.sequence = 1; assert(send(&f, req, 102).status == HB_MUSE_WIRE_STALE);
    assert(f.server.last_sequence == 2 && f.server.last_activity_ms == 101 && f.fake.dispatch_calls == 1);
    req.sequence = 3; assert(send(&f, req, 103).status == HB_MUSE_WIRE_OK);
}
static void test_dispatch_statuses_cached_consume_sequence(void) {
    const hb_muse_wire_status_t statuses[] = {HB_MUSE_WIRE_OK, HB_MUSE_WIRE_BLOCKED,
        HB_MUSE_WIRE_BUSY, HB_MUSE_WIRE_REJECTED};
    for (unsigned i = 0; i < sizeof(statuses) / sizeof(statuses[0]); ++i) {
        fixture_t f; init(&f, 0); open_at(&f, 1, 100); f.fake.dispatch_status = statuses[i];
        hb_muse_wire_frame_t req = request(HB_MUSE_WIRE_PCM, 1, 2);
        assert(send(&f, req, 101).status == statuses[i]);
        f.fake.dispatch_status = HB_MUSE_WIRE_OK;
        assert(send(&f, req, 102).status == statuses[i] && f.fake.dispatch_calls == 1);
        req.sequence = 3; assert(send(&f, req, 103).status == HB_MUSE_WIRE_OK);
        assert(f.fake.dispatch_calls == 2 && f.server.last_sequence == 3);
    }
}
static void test_stale_epoch_boot_and_active_open(void) {
    fixture_t f; init(&f, 0); open_at(&f, 4, 100);
    hb_muse_wire_frame_t req = request(HB_MUSE_WIRE_STATUS, 4, 2); ++req.boot;
    assert(send(&f, req, 101).status == HB_MUSE_WIRE_STALE);
    req.boot = BOOT; req.epoch = 3; assert(send(&f, req, 101).status == HB_MUSE_WIRE_STALE);
    req.epoch = 5; assert(send(&f, req, 101).status == HB_MUSE_WIRE_STALE);
    assert(send(&f, request(HB_MUSE_WIRE_OPEN, 5, 1), 101).status == HB_MUSE_WIRE_BUSY);
    assert(f.server.max_epoch == 4 && f.server.last_sequence == 1 && f.fake.can_open_calls == 1);
    assert(!f.fake.dispatch_calls && f.server.last_activity_ms == 100);
}
static void test_close_fence_cache_tombstone(void) {
    fixture_t f; init(&f, 0); open_at(&f, 5, 100); f.fake.fenced = false;
    hb_muse_wire_frame_t close = request(HB_MUSE_WIRE_CLOSE, 5, 2);
    assert(send(&f, close, 101).status == HB_MUSE_WIRE_BUSY && f.server.active);
    f.fake.fenced = true;
    assert(send(&f, close, 102).status == HB_MUSE_WIRE_BUSY); /* exact cached result */
    assert(f.fake.can_open_calls == 2);
    close.sequence = 3; assert(send(&f, close, 103).status == HB_MUSE_WIRE_OK);
    assert(!f.server.active && f.server.max_epoch == 5 && !f.fake.dispatch_calls && !f.fake.revoke_calls);
    unsigned calls = f.fake.can_open_calls;
    assert(send(&f, close, 5000).status == HB_MUSE_WIRE_OK && f.fake.can_open_calls == calls);
    close.payload_len = 1; assert(send(&f, close, 5001).status == HB_MUSE_WIRE_CONFLICT);
    assert(send(&f, request(HB_MUSE_WIRE_OPEN, 5, 1), 5002).status == HB_MUSE_WIRE_STALE);
    assert(send(&f, request(HB_MUSE_WIRE_STATUS, 5, 4), 5002).status == HB_MUSE_WIRE_STALE);
    open_at(&f, 6, 5003);
    assert(send(&f, request(HB_MUSE_WIRE_CLOSE, 5, 3), 5004).status == HB_MUSE_WIRE_STALE);
}
static void test_invalid_close_consumes_without_action(void) {
    fixture_t f; init(&f, 0); open_at(&f, 1, 100);
    hb_muse_wire_frame_t close = request(HB_MUSE_WIRE_CLOSE, 1, 2); close.turn = 1;
    assert(send(&f, close, 101).status == HB_MUSE_WIRE_REJECTED && f.server.last_sequence == 2);
    assert(send(&f, close, 102).status == HB_MUSE_WIRE_REJECTED && f.fake.can_open_calls == 1);
    close.turn = 0; close.payload_len = 1;
    assert(send(&f, close, 103).status == HB_MUSE_WIRE_CONFLICT && f.server.last_sequence == 2);
    close.sequence = 3;
    assert(send(&f, close, 104).status == HB_MUSE_WIRE_REJECTED && f.fake.can_open_calls == 1);
    assert(f.server.active && f.server.last_sequence == 3 && !f.fake.revoke_calls);
    close.sequence = 4; close.payload_len = 0;
    assert(send(&f, close, 105).status == HB_MUSE_WIRE_OK);
}
static void test_lease_duplicate_hello_never_refresh(void) {
    fixture_t f; init(&f, 0); open_at(&f, 1, 100);
    hb_muse_wire_frame_t req = request(HB_MUSE_WIRE_STATUS, 1, 2);
    assert(send(&f, req, 200).status == HB_MUSE_WIRE_OK);
    assert(send(&f, req, 2100).status == HB_MUSE_WIRE_OK);
    assert(send(&f, hello(), 2199).payload[4] == 1);
    assert(f.server.last_activity_ms == 200 && !f.fake.revoke_calls);
    hb_muse_wire_server_expire(&f.server, 2200);
    assert(!f.server.active && f.fake.revoke_calls == 1 && f.server.max_epoch == 1);
    hb_muse_wire_server_expire(&f.server, 5000); assert(f.fake.revoke_calls == 1);
    assert(send(&f, req, 5001).status == HB_MUSE_WIRE_STALE && f.fake.dispatch_calls == 1);
    assert(send(&f, request(HB_MUSE_WIRE_OPEN, 1, 1), 5001).status == HB_MUSE_WIRE_STALE);
    assert(send(&f, request(HB_MUSE_WIRE_OPEN, 2, 1), 5002).status == HB_MUSE_WIRE_BUSY);
    f.fake.fenced = true; open_at(&f, 2, 5003);
}
static void test_expiry_on_request_before_callback(void) {
    fixture_t f; init(&f, 500); open_at(&f, 1, 100);
    assert(send(&f, request(HB_MUSE_WIRE_PCM, 1, 2), 600).status == HB_MUSE_WIRE_STALE);
    assert(f.fake.revoke_calls == 1 && !f.fake.dispatch_calls && !f.server.active);
}
static void test_new_sequence_refreshes_lease(void) {
    fixture_t f; init(&f, 500); open_at(&f, 1, 100);
    f.fake.dispatch_status = HB_MUSE_WIRE_BLOCKED;
    assert(send(&f, request(HB_MUSE_WIRE_PCM, 1, 2), 599).status == HB_MUSE_WIRE_BLOCKED);
    hb_muse_wire_server_expire(&f.server, 1098); assert(f.server.active);
    hb_muse_wire_server_expire(&f.server, 1099); assert(!f.server.active && f.fake.revoke_calls == 1);
}
static void test_backward_clock_fault_and_no_reopen(void) {
    fixture_t f; init(&f, 0); open_at(&f, 1, 1000);
    hb_muse_wire_server_expire(&f.server, 999);
    assert(f.server.clock_fault && !f.server.active && f.fake.revoke_calls == 1);
    f.fake.fenced = true;
    assert(send(&f, request(HB_MUSE_WIRE_OPEN, 2, 1), 2000).status == HB_MUSE_WIRE_REJECTED);
    assert(send(&f, hello(), 2001).payload[4] == 0);
    hb_muse_wire_server_expire(&f.server, 998); assert(f.fake.revoke_calls == 1);
    fixture_t idle; init(&idle, 0);
    hb_muse_wire_server_expire(&idle.server, 10); hb_muse_wire_server_expire(&idle.server, 9);
    assert(idle.server.clock_fault && !idle.fake.revoke_calls);
}
static void test_sequence_and_time_overflow_no_wrap(void) {
    fixture_t f; init(&f, 10); open_at(&f, UINT32_MAX - 1u, UINT64_MAX - 20u);
    f.server.last_sequence = UINT32_MAX - 1u; f.server.cache_valid = false;
    hb_muse_wire_frame_t req = request(HB_MUSE_WIRE_STATUS, UINT32_MAX - 1u, UINT32_MAX);
    assert(send(&f, req, UINT64_MAX - 19u).status == HB_MUSE_WIRE_OK);
    assert(send(&f, req, UINT64_MAX - 18u).status == HB_MUSE_WIRE_OK && f.fake.dispatch_calls == 1);
    req.sequence = 0; assert(send(&f, req, UINT64_MAX - 17u).status == HB_MUSE_WIRE_STALE);
    req.sequence = 1; assert(send(&f, req, UINT64_MAX - 16u).status == HB_MUSE_WIRE_STALE);
    hb_muse_wire_server_expire(&f.server, UINT64_MAX - 9u);
    assert(!f.server.active && f.fake.revoke_calls == 1);
    f.fake.fenced = true;
    assert(send(&f, request(HB_MUSE_WIRE_OPEN, UINT32_MAX, 1), UINT64_MAX).status == HB_MUSE_WIRE_REJECTED);
}
static void test_reboot_different_boot_rejects_old_bytes(void) {
    fixture_t old, fresh; init(&old, 0); open_at(&old, 1, 100);
    hb_muse_wire_frame_t req = request(HB_MUSE_WIRE_BEGIN, 1, 2);
    assert(send(&old, req, 101).status == HB_MUSE_WIRE_OK);
    init(&fresh, 0); fresh.server.boot = BOOT + 1u;
    uint8_t bytes[248], reply[248]; size_t n = encode(&req, bytes), reply_n;
    assert(hb_muse_wire_server_process(&fresh.server, bytes, n, 1, reply, 248, &reply_n));
    hb_muse_wire_frame_t out; assert(hb_muse_wire_decode(reply, reply_n, &out));
    assert(out.status == HB_MUSE_WIRE_STALE && out.boot == BOOT + 1u);
    assert(!fresh.fake.dispatch_calls && !fresh.server.max_epoch);
}
static void test_bad_callback_revokes_uncertain_acceptance(void) {
    for (unsigned mode = 0; mode < 2; ++mode) {
        fixture_t f; init(&f, 0); open_at(&f, 1, 100);
        if (mode == 0) f.fake.reply_len = 217;
        else f.fake.dispatch_status = HB_MUSE_WIRE_CONFLICT;
        hb_muse_wire_frame_t req = request(HB_MUSE_WIRE_PCM, 1, 2);
        assert(send(&f, req, 101).status == HB_MUSE_WIRE_REJECTED);
        assert(!f.server.active && f.fake.revoke_calls == 1 && f.fake.dispatch_calls == 1);
        assert(send(&f, req, 102).status == HB_MUSE_WIRE_STALE && f.fake.dispatch_calls == 1);
    }
}
static void test_all_application_opcodes_and_max_response(void) {
    fixture_t f; init(&f, 0); open_at(&f, 1, 100);
    f.fake.reply_len = 216; memset(f.fake.reply, 0xc7, 216);
    uint32_t sequence = 2;
    for (uint8_t op = HB_MUSE_WIRE_BEGIN; op <= HB_MUSE_WIRE_RETIRE; ++op) {
        hb_muse_wire_frame_t req = request(op, 1, sequence++);
        hb_muse_wire_frame_t out = send(&f, req, 101);
        assert(out.status == HB_MUSE_WIRE_OK && out.payload_len == 216);
        assert(!memcmp(out.payload, f.fake.reply, 216));
    }
    assert(f.fake.dispatch_calls == 7);
}
static void test_opened_notification_only_new_epoch(void) {
    fixture_t f; init(&f, 0);
    assert(send(&f, hello(), 1).status == HB_MUSE_WIRE_OK && !f.fake.opened_calls);
    f.fake.fenced = false;
    assert(send(&f, request(HB_MUSE_WIRE_OPEN, 4, 1), 2).status == HB_MUSE_WIRE_BUSY);
    assert(!f.fake.opened_calls); f.fake.fenced = true;
    open_at(&f, 4, 3); assert(f.fake.opened_calls == 1 && f.fake.opened_epoch == 4);
    assert(send(&f, request(HB_MUSE_WIRE_OPEN, 4, 1), 4).status == HB_MUSE_WIRE_OK);
    assert(f.fake.opened_calls == 1);
    assert(send(&f, request(HB_MUSE_WIRE_CLOSE, 4, 2), 5).status == HB_MUSE_WIRE_OK);
    open_at(&f, 5, 6); assert(f.fake.opened_calls == 2 && f.fake.opened_epoch == 5);
    assert(send(&f, request(HB_MUSE_WIRE_OPEN, 4, 1), 7).status == HB_MUSE_WIRE_STALE);
    assert(f.fake.opened_calls == 2);
    fixture_t optional; init(&optional, 0); optional.server.callbacks.opened = NULL;
    open_at(&optional, 1, 1); assert(!optional.fake.opened_calls);
}
static void test_nonce_shapes_echo_and_read_only_discovery(void) {
    fixture_t f; init(&f, 0);
    hb_muse_wire_frame_t h = hello();
    hb_muse_wire_server_t before = f.server;
    hb_muse_wire_frame_t out = send(&f, h, UINT64_MAX);
    assert(out.payload_len == 13 && !memcmp(out.payload + 5, h.payload, 8));
    assert(!memcmp(&before, &f.server, sizeof(before)));
    h.payload_len = 0; assert(send(&f, h, 1).status == HB_MUSE_WIRE_REJECTED);
    h.payload_len = 7; assert(send(&f, h, 1).status == HB_MUSE_WIRE_REJECTED);
    h.payload_len = 9; assert(send(&f, h, 1).status == HB_MUSE_WIRE_REJECTED);
    h.payload_len = 8; memset(h.payload, 0, 8);
    assert(send(&f, h, 1).status == HB_MUSE_WIRE_REJECTED);
    assert(!memcmp(&before, &f.server, sizeof(before)));
    hb_muse_wire_frame_t o = request(HB_MUSE_WIRE_OPEN, 1, 1);
    o.payload_len = 0; assert(send(&f, o, 1).status == HB_MUSE_WIRE_REJECTED);
    o.payload_len = 8; memset(o.payload, 0, 8);
    assert(send(&f, o, 1).status == HB_MUSE_WIRE_REJECTED && !f.fake.opened_calls);
    memcpy(o.payload, NONCE, 8); f.fake.fenced = false;
    out = send(&f, o, 2);
    assert(out.status == HB_MUSE_WIRE_BUSY && out.payload_len == 8 && !memcmp(out.payload, NONCE, 8));
    f.fake.fenced = true; out = send(&f, o, 3);
    assert(out.status == HB_MUSE_WIRE_OK && out.payload_len == 8 && !memcmp(out.payload, NONCE, 8));
    before = f.server; h = hello(); h.payload[7] ^= 0x80;
    out = send(&f, h, UINT64_MAX);
    assert(out.payload_len == 13 && !memcmp(out.payload + 5, h.payload, 8));
    assert(!memcmp(&before, &f.server, sizeof(before)) && !f.fake.revoke_calls);
    o.payload[7] ^= 0x80; out = send(&f, o, 4);
    assert(out.status == HB_MUSE_WIRE_CONFLICT && out.payload_len == 8);
    assert(!memcmp(out.payload, o.payload, 8) && f.fake.opened_calls == 1);
}
static void test_cache_tail_and_revocation_clear_retained_payload(void) {
    fixture_t f; init(&f, 0); open_at(&f, 1, 100);
    hb_muse_wire_frame_t pcm = request(HB_MUSE_WIRE_PCM, 1, 2);
    pcm.payload_len = 216; memset(pcm.payload, 0xee, sizeof(pcm.payload));
    f.fake.reply_len = 216; memset(f.fake.reply, 0xcc, sizeof(f.fake.reply));
    assert(send(&f, pcm, 101).status == HB_MUSE_WIRE_OK);
    assert(f.server.cached_request_len == 248 && f.server.cached_response_len == 248);
    f.fake.reply_len = 0;
    assert(send(&f, request(HB_MUSE_WIRE_CANCEL, 1, 3), 102).status == HB_MUSE_WIRE_OK);
    assert(f.server.cached_request_len == 32 && f.server.cached_response_len == 32);
    for (size_t i = 32; i < HB_MUSE_WIRE_FRAME_MAX; ++i)
        assert(!f.server.cached_request[i] && !f.server.cached_response[i]);
    pcm.sequence = 4; f.fake.reply_len = 216;
    assert(send(&f, pcm, 103).status == HB_MUSE_WIRE_OK);
    hb_muse_wire_server_expire(&f.server, 2103);
    assert(!f.server.cache_valid && !f.server.cached_request_len && !f.server.cached_response_len);
    for (size_t i = 0; i < HB_MUSE_WIRE_FRAME_MAX; ++i)
        assert(!f.server.cached_request[i] && !f.server.cached_response[i]);
}

int main(void) {
    const struct { const char *name; void (*run)(void); } tests[] = {
        {"CRC/header layout", test_crc_and_exact_header},
        {"codec bounds", test_codec_bounds_and_no_partial_write},
        {"corruption/truncation", test_corruption_and_malformed_decode_preserve_output},
        {"initialization", test_init_validation},
        {"HELLO/OPEN guard", test_hello_and_open_guard},
        {"invalid controls", test_invalid_control_and_response_requests},
        {"small output/bad CRC nonmutation", test_small_response_buffer_and_bad_crc_no_mutation},
        {"lost response exact replay", test_response_loss_exact_replay_no_side_effect},
        {"OPEN replay/in-place buffer", test_duplicate_open_and_alias_input_output},
        {"conflict/gap/stale sequence", test_conflicts_and_gaps},
        {"cached dispatch statuses", test_dispatch_statuses_cached_consume_sequence},
        {"stale boot/epoch", test_stale_epoch_boot_and_active_open},
        {"CLOSE fence/tombstone", test_close_fence_cache_tombstone},
        {"invalid CLOSE consumed/no action", test_invalid_close_consumes_without_action},
        {"lease duplicate/HELLO", test_lease_duplicate_hello_never_refresh},
        {"expiry before dispatch", test_expiry_on_request_before_callback},
        {"accepted sequence refresh", test_new_sequence_refreshes_lease},
        {"backward clock", test_backward_clock_fault_and_no_reopen},
        {"sequence/time overflow", test_sequence_and_time_overflow_no_wrap},
        {"reboot boot ID", test_reboot_different_boot_rejects_old_bytes},
        {"callback violation", test_bad_callback_revokes_uncertain_acceptance},
        {"application opcodes/max response", test_all_application_opcodes_and_max_response},
        {"new OPEN notification", test_opened_notification_only_new_epoch},
        {"discovery nonce/readonly HELLO", test_nonce_shapes_echo_and_read_only_discovery},
        {"cache payload retention", test_cache_tail_and_revocation_clear_retained_payload},
    };
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        printf("RUN %s\n", tests[i].name); fflush(stdout); tests[i].run();
    }
    printf("PASS %zu wire codec/server groups (explicit application fake, no hardware)\n",
           sizeof(tests) / sizeof(tests[0]));
    return 0;
}
