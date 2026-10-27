#include "hb_muse_wire_client.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    hb_muse_wire_client_t client;
    hb_muse_wire_server_t server;
    unsigned calls, revokes;
    bool fenced;
    hb_muse_wire_status_t status;
} fixture_t;
static unsigned groups;
static uint64_t next_nonce = UINT64_C(0xabcdef0000000000);
static bool hello(fixture_t *f, uint64_t now) {
    return hb_muse_wire_client_prepare_hello(&f->client, ++next_nonce, now);
}
static bool can_open(void *user) { return ((fixture_t *)user)->fenced; }
static hb_muse_wire_status_t dispatch(void *user, const hb_muse_wire_frame_t *request,
                                    uint8_t *reply, uint16_t *length) {
    fixture_t *f = user;
    ++f->calls;
    *length = request->payload_len;
    memcpy(reply, request->payload, request->payload_len);
    return f->status;
}
static void revoke(void *user) { ++((fixture_t *)user)->revokes; }
static void init(fixture_t *f) {
    memset(f, 0, sizeof(*f));
    f->fenced = true;
    hb_muse_wire_callbacks_t callbacks = {
        .can_open = can_open, .dispatch = dispatch, .revoke = revoke
    };
    assert(hb_muse_wire_server_init(&f->server, 77, 0, &callbacks, f));
    assert(hb_muse_wire_client_init(&f->client, 0));
}
static size_t request(fixture_t *f, uint64_t now, uint8_t *bytes) {
    size_t length = 0;
    assert(hb_muse_wire_client_next_request(&f->client, now, bytes,
                                           HB_MUSE_WIRE_FRAME_MAX, &length));
    return length;
}
static size_t server_reply(fixture_t *f, const uint8_t *bytes, size_t length,
                           uint64_t now, uint8_t *reply) {
    size_t reply_len = 0;
    assert(hb_muse_wire_server_process(&f->server, bytes, length, now, reply,
                                      HB_MUSE_WIRE_FRAME_MAX, &reply_len));
    return reply_len;
}
static hb_muse_wire_frame_t exchange(fixture_t *f, uint64_t now) {
    uint8_t bytes[HB_MUSE_WIRE_FRAME_MAX], reply[HB_MUSE_WIRE_FRAME_MAX];
    size_t length = request(f, now, bytes);
    size_t reply_len = server_reply(f, bytes, length, now, reply);
    hb_muse_wire_frame_t out;
    assert(hb_muse_wire_client_accept_response(&f->client, reply, reply_len, now, &out)
           == HB_MUSE_CLIENT_RESPONSE);
    return out;
}
static void open_client(fixture_t *f, uint64_t now) {
    assert(hello(f, now));
    assert(exchange(f, now).status == HB_MUSE_WIRE_OK);
    assert(f->client.state == HB_MUSE_CLIENT_CAN_OPEN);
    assert(hb_muse_wire_client_prepare_open(&f->client, now));
    assert(exchange(f, now).status == HB_MUSE_WIRE_OK);
    assert(f->client.state == HB_MUSE_CLIENT_ACTIVE);
    assert(f->calls == 0); /* OPEN is not permission to start recording. */
}
static void command(fixture_t *f, uint64_t now, uint8_t opcode) {
    const uint8_t payload[] = {1, 2, 3};
    assert(hb_muse_wire_client_prepare_command(&f->client, opcode, 4, 5,
                                              payload, sizeof(payload), now));
}
static size_t encode(const hb_muse_wire_frame_t *f, uint8_t *bytes) {
    size_t length = 0;
    assert(hb_muse_wire_encode(f, bytes, HB_MUSE_WIRE_FRAME_MAX, &length));
    return length;
}
static void assert_ignored(fixture_t *f, const uint8_t *bytes, size_t length, uint64_t now) {
    hb_muse_wire_frame_t out, before;
    memset(&out, 0xa7, sizeof(out)); before = out;
    assert(hb_muse_wire_client_accept_response(&f->client, bytes, length, now, &out)
           == HB_MUSE_CLIENT_IGNORED);
    assert(!memcmp(&out, &before, sizeof(out)));
}

static void happy_path(void) {
    fixture_t f; init(&f); open_client(&f, 0);
    command(&f, 1, HB_MUSE_WIRE_BEGIN);
    hb_muse_wire_frame_t out = exchange(&f, 1);
    assert(out.status == HB_MUSE_WIRE_OK && out.sequence == 2 && out.payload_len == 3);
    assert(f.client.last_sequence == 2 && f.calls == 1 && f.client.lease_start_ms == 1);
    command(&f, 2, HB_MUSE_WIRE_PCM);
    assert(exchange(&f, 2).sequence == 3);
    assert(f.calls == 2);
    ++groups;
}
static void bounded_identical_retry(void) {
    fixture_t f; init(&f); open_client(&f, 0); command(&f, 1, HB_MUSE_WIRE_PCM);
    uint8_t original[248], retry[248], reply[248];
    size_t length = request(&f, 1, original);
    size_t reply_len = server_reply(&f, original, length, 1, reply);
    assert(f.calls == 1);
    size_t retry_len = 9;
    assert(!hb_muse_wire_client_next_request(&f.client, 250, retry, sizeof(retry), &retry_len));
    assert(retry_len == 0);
    retry_len = request(&f, 251, retry);
    assert(length == retry_len && !memcmp(original, retry, length));
    uint8_t repeated[248];
    size_t repeated_len = server_reply(&f, retry, retry_len, 251, repeated);
    assert(reply_len == repeated_len && !memcmp(reply, repeated, reply_len));
    assert(f.calls == 1 && f.server.last_activity_ms == 1);
    hb_muse_wire_frame_t out;
    assert(hb_muse_wire_client_accept_response(&f.client, repeated, repeated_len, 251, &out)
           == HB_MUSE_CLIENT_RESPONSE);
    assert(f.client.lease_start_ms == 1); /* Cached ACK did not renew it. */
    ++groups;
}
static void lost_open_response(void) {
    fixture_t f; init(&f);
    assert(hello(&f, 10)); exchange(&f, 10);
    assert(hb_muse_wire_client_prepare_open(&f.client, 10));
    uint8_t original[248], retry[248], reply[248];
    size_t length = request(&f, 10, original);
    server_reply(&f, original, length, 10, reply);
    assert(f.server.active && !f.calls);
    size_t retry_len = request(&f, 260, retry);
    assert(length == retry_len && !memcmp(original, retry, length));
    size_t reply_len = server_reply(&f, retry, retry_len, 260, reply);
    hb_muse_wire_frame_t out;
    assert(hb_muse_wire_client_accept_response(&f.client, reply, reply_len, 260, &out)
           == HB_MUSE_CLIENT_RESPONSE);
    assert(f.client.state == HB_MUSE_CLIENT_ACTIVE && f.client.epoch == 1);
    assert(f.client.lease_start_ms == 10 && f.server.max_epoch == 1);
    ++groups;
}
static void timeout_no_new_audio(void) {
    fixture_t f; init(&f); open_client(&f, 0); command(&f, 1, HB_MUSE_WIRE_PCM);
    uint8_t bytes[248], reply[248];
    size_t length = request(&f, 1, bytes);
    size_t reply_len = server_reply(&f, bytes, length, 1, reply);
    request(&f, 251, bytes); request(&f, 501, bytes);
    size_t none = 99;
    assert(!hb_muse_wire_client_next_request(&f.client, 600, bytes, sizeof(bytes), &none));
    assert(f.client.attempts == 3 && !none);
    assert_ignored(&f, reply, reply_len, 751); /* Late success is not accepted. */
    assert(f.client.fault == HB_MUSE_CLIENT_FAULT_TIMEOUT);
    assert(!f.client.pending && !f.client.encoded_len);
    assert(!hb_muse_wire_client_prepare_command(&f.client, HB_MUSE_WIRE_END, 4, 5, NULL, 0, 751));
    assert(!hb_muse_wire_client_prepare_open(&f.client, 751));
    assert(hello(&f, 751)); exchange(&f, 751);
    assert(f.client.state == HB_MUSE_CLIENT_PEER_BUSY);
    assert(!hb_muse_wire_client_prepare_open(&f.client, 751));
    ++groups;
}
static void crc_and_foreign_never_expose(void) {
    fixture_t f; init(&f); open_client(&f, 0); command(&f, 10, HB_MUSE_WIRE_PCM);
    uint8_t bytes[248], reply[248], changed[248];
    size_t length = request(&f, 10, bytes);
    size_t reply_len = server_reply(&f, bytes, length, 10, reply);
    memcpy(changed, reply, reply_len); changed[reply_len - 1] ^= 1u;
    assert_ignored(&f, changed, reply_len, 10);
    assert_ignored(&f, reply, reply_len - 1, 10);
    assert_ignored(&f, bytes, length, 10); /* A request is not an ACK. */
    hb_muse_wire_frame_t decoded;
    assert(hb_muse_wire_decode(reply, reply_len, &decoded));
    for (unsigned kind = 0; kind < 6; ++kind) {
        hb_muse_wire_frame_t wrong = decoded;
        if (kind == 0) ++wrong.sequence;
        if (kind == 1) ++wrong.epoch;
        if (kind == 2) ++wrong.generation;
        if (kind == 3) ++wrong.turn;
        if (kind == 4) ++wrong.boot;
        if (kind == 5) wrong.opcode = HB_MUSE_WIRE_END;
        size_t changed_len = encode(&wrong, changed);
        assert_ignored(&f, changed, changed_len, 10);
    }
    assert(f.client.pending && f.client.last_sequence == 1 && f.client.lease_start_ms == 0);
    hb_muse_wire_frame_t out;
    assert(hb_muse_wire_client_accept_response(&f.client, reply, reply_len, 10, &out)
           == HB_MUSE_CLIENT_RESPONSE);
    ++groups;
}
static void fatal_statuses(void) {
    const uint8_t statuses[] = {HB_MUSE_WIRE_CONFLICT, HB_MUSE_WIRE_GAP, HB_MUSE_WIRE_STALE};
    for (size_t i = 0; i < sizeof(statuses); ++i) {
        fixture_t f; init(&f); open_client(&f, 0); command(&f, 1, HB_MUSE_WIRE_END);
        uint8_t bytes[248]; request(&f, 1, bytes);
        hb_muse_wire_frame_t response = f.client.request;
        response.flags = HB_MUSE_WIRE_RESPONSE; response.status = statuses[i];
        size_t length = encode(&response, bytes);
        hb_muse_wire_frame_t out, before;
        memset(&out, 0x5a, sizeof(out)); before = out;
        assert(hb_muse_wire_client_accept_response(&f.client, bytes, length, 1, &out)
               == HB_MUSE_CLIENT_LINK_LOST);
        assert(!memcmp(&out, &before, sizeof(out)));
        assert(f.client.state == HB_MUSE_CLIENT_NEED_HELLO && !f.client.pending);
    }
    ++groups;
}
static void rejection_and_blocked(void) {
    fixture_t f; init(&f); open_client(&f, 0);
    f.status = HB_MUSE_WIRE_BLOCKED; command(&f, 1, HB_MUSE_WIRE_PCM);
    assert(exchange(&f, 1).status == HB_MUSE_WIRE_BLOCKED);
    assert(f.client.last_sequence == 2 && f.client.state == HB_MUSE_CLIENT_ACTIVE);
    f.status = HB_MUSE_WIRE_BUSY; command(&f, 2, HB_MUSE_WIRE_PCM);
    assert(exchange(&f, 2).status == HB_MUSE_WIRE_BUSY);
    assert(f.client.last_sequence == 3);
    f.status = HB_MUSE_WIRE_REJECTED; command(&f, 3, HB_MUSE_WIRE_PCM);
    assert(exchange(&f, 3).status == HB_MUSE_WIRE_REJECTED);
    assert(f.client.state == HB_MUSE_CLIENT_NEED_HELLO);
    ++groups;
}
static void reboot_mid_command(void) {
    fixture_t f; init(&f); open_client(&f, 0); command(&f, 1, HB_MUSE_WIRE_PCM);
    uint8_t bytes[248], reply[248]; size_t length = request(&f, 1, bytes);
    hb_muse_wire_callbacks_t callbacks = f.server.callbacks;
    assert(hb_muse_wire_server_init(&f.server, 78, 0, &callbacks, &f));
    size_t reply_len = server_reply(&f, bytes, length, 1, reply);
    hb_muse_wire_frame_t out;
    assert(hb_muse_wire_client_accept_response(&f.client, reply, reply_len, 1, &out)
           == HB_MUSE_CLIENT_LINK_LOST);
    assert(f.client.fault == HB_MUSE_CLIENT_FAULT_REBOOT && !f.calls);
    open_client(&f, 2);
    assert(f.client.boot == 78 && f.client.epoch == 1);
    ++groups;
}
static void abandon_fences_then_reopens(void) {
    fixture_t f; init(&f); open_client(&f, 0); command(&f, 1, HB_MUSE_WIRE_PCM);
    uint8_t bytes[248], reply[248]; size_t length = request(&f, 1, bytes);
    server_reply(&f, bytes, length, 1, reply);
    f.fenced = false;
    hb_muse_wire_client_abandon(&f.client);
    for (size_t i = 0; i < sizeof(f.client.encoded); ++i) assert(!f.client.encoded[i]);
    size_t none = 1;
    assert(!hb_muse_wire_client_next_request(&f.client, 251, bytes, sizeof(bytes), &none));
    assert(!none && f.calls == 1);
    hb_muse_wire_server_expire(&f.server, 2001);
    assert(f.revokes == 1 && !f.server.active);
    assert(hello(&f, 2001)); exchange(&f, 2001);
    assert(hb_muse_wire_client_prepare_open(&f.client, 2001));
    assert(exchange(&f, 2001).status == HB_MUSE_WIRE_BUSY);
    assert(f.client.state == HB_MUSE_CLIENT_PEER_BUSY);
    f.fenced = true;
    assert(hello(&f, 2002)); exchange(&f, 2002);
    assert(hb_muse_wire_client_prepare_open(&f.client, 2002)); exchange(&f, 2002);
    assert(f.client.epoch == 2 && f.calls == 1);
    ++groups;
}
static void idle_lease_expiry(void) {
    fixture_t f; init(&f); open_client(&f, 0);
    hb_muse_wire_client_tick(&f.client, 1999);
    assert(f.client.state == HB_MUSE_CLIENT_ACTIVE);
    hb_muse_wire_client_tick(&f.client, 2000);
    assert(f.client.fault == HB_MUSE_CLIENT_FAULT_LEASE);
    assert(!hb_muse_wire_client_prepare_command(&f.client, HB_MUSE_WIRE_PCM, 1, 1, NULL, 0, 2000));
    ++groups;
}
static void pending_unsent_lease_expiry(void) {
    fixture_t f; init(&f); open_client(&f, 0); command(&f, 1999, HB_MUSE_WIRE_PCM);
    uint8_t bytes[248]; size_t length = 99;
    assert(!hb_muse_wire_client_next_request(&f.client, 2000, bytes, sizeof(bytes), &length));
    assert(length == 0 && f.client.fault == HB_MUSE_CLIENT_FAULT_LEASE && !f.calls);
    ++groups;
}
static void rollback_permanent_until_init(void) {
    fixture_t f; init(&f); open_client(&f, 100);
    hb_muse_wire_client_tick(&f.client, 99);
    assert(f.client.clock_fault && f.client.fault == HB_MUSE_CLIENT_FAULT_CLOCK);
    assert(!hello(&f, 200));
    hb_muse_wire_client_abandon(&f.client);
    assert(f.client.fault == HB_MUSE_CLIENT_FAULT_CLOCK);
    assert(hb_muse_wire_client_init(&f.client, 0));
    assert(hello(&f, 1));
    ++groups;
}
static void overflow_and_limits(void) {
    fixture_t f; init(&f); open_client(&f, 0);
    f.client.last_sequence = UINT32_MAX;
    assert(!hb_muse_wire_client_prepare_command(&f.client, HB_MUSE_WIRE_PCM, 1, 1, NULL, 0, 0));
    assert(f.client.fault == HB_MUSE_CLIENT_FAULT_EXHAUSTED);
    for (uint32_t n = 0; n < 2; ++n) {
        assert(hb_muse_wire_client_init(&f.client, 0));
        f.client.state = HB_MUSE_CLIENT_CAN_OPEN;
        f.client.boot = 77; f.client.max_epoch = UINT32_MAX - n;
        assert(!hb_muse_wire_client_prepare_open(&f.client, 0));
        assert(f.client.fault == HB_MUSE_CLIENT_FAULT_EXHAUSTED);
    }
    assert(!hb_muse_wire_client_init(&f.client, 750));
    assert(hb_muse_wire_client_init(&f.client, 751));
    assert(!hb_muse_wire_client_init(NULL, 0));
    ++groups;
}
static void no_concurrent_or_unprepared_send(void) {
    fixture_t f; init(&f); uint8_t bytes[248]; size_t length = 9;
    assert(!hb_muse_wire_client_next_request(&f.client, 0, bytes, sizeof(bytes), &length));
    assert(!length);
    open_client(&f, 0); command(&f, 1, HB_MUSE_WIRE_BEGIN);
    assert(!hb_muse_wire_client_prepare_command(&f.client, HB_MUSE_WIRE_END, 1, 1, NULL, 0, 1));
    assert(!hello(&f, 1));
    assert(!hb_muse_wire_client_prepare_open(&f.client, 1));
    assert(!hb_muse_wire_client_next_request(&f.client, 1, bytes, 1, &length));
    assert(!length && f.client.attempts == 0);
    request(&f, 1, bytes);
    assert(f.client.attempts == 1);
    ++groups;
}
static void malformed_hello(void) {
    fixture_t f; init(&f);
    assert(hello(&f, 0));
    uint8_t bytes[248], reply[248]; size_t length = request(&f, 0, bytes);
    size_t reply_len = server_reply(&f, bytes, length, 0, reply);
    hb_muse_wire_frame_t good;
    assert(hb_muse_wire_decode(reply, reply_len, &good));
    for (unsigned variant = 0; variant < 5; ++variant) {
        hb_muse_wire_frame_t bad = good;
        if (variant == 0) bad.payload_len = 4;
        if (variant == 1) bad.payload[4] = 2;
        if (variant == 2) bad.boot = 0;
        if (variant == 3) bad.status = HB_MUSE_WIRE_BUSY;
        if (variant == 4) bad.generation = 1;
        size_t bad_len = encode(&bad, bytes);
        assert_ignored(&f, bytes, bad_len, 0);
    }
    hb_muse_wire_frame_t out;
    assert(hb_muse_wire_client_accept_response(&f.client, reply, reply_len, 0, &out)
           == HB_MUSE_CLIENT_RESPONSE);
    assert(f.client.boot == 77);
    ++groups;
}
static void old_duplicate_ignored(void) {
    fixture_t f; init(&f); open_client(&f, 0); command(&f, 1, HB_MUSE_WIRE_PCM);
    uint8_t bytes[248], reply[248]; size_t length = request(&f, 1, bytes);
    size_t reply_len = server_reply(&f, bytes, length, 1, reply);
    hb_muse_wire_frame_t out;
    assert(hb_muse_wire_client_accept_response(&f.client, reply, reply_len, 1, &out)
           == HB_MUSE_CLIENT_RESPONSE);
    assert_ignored(&f, reply, reply_len, 2);
    command(&f, 2, HB_MUSE_WIRE_PCM); request(&f, 2, bytes);
    assert_ignored(&f, reply, reply_len, 2);
    assert(f.client.pending && f.client.last_sequence == 2);
    ++groups;
}
static void maximum_payload_and_validation(void) {
    fixture_t f; init(&f); open_client(&f, 0);
    uint8_t payload[216]; memset(payload, 0x7e, sizeof(payload));
    assert(!hb_muse_wire_client_prepare_command(&f.client, HB_MUSE_WIRE_PCM, 1, 1, payload, 217, 0));
    assert(!hb_muse_wire_client_prepare_command(&f.client, HB_MUSE_WIRE_PCM, 1, 1, NULL, 1, 0));
    assert(!hb_muse_wire_client_prepare_command(&f.client, HB_MUSE_WIRE_OPEN, 0, 0, NULL, 0, 0));
    assert(!hb_muse_wire_client_prepare_command(&f.client, HB_MUSE_WIRE_CLOSE, 1, 1, NULL, 0, 0));
    assert(hb_muse_wire_client_prepare_command(&f.client, HB_MUSE_WIRE_PCM, 1, 1,
                                              payload, sizeof(payload), 0));
    assert(f.client.encoded_len == 248);
    hb_muse_wire_frame_t out = exchange(&f, 0);
    assert(out.payload_len == 216 && !memcmp(payload, out.payload, sizeof(payload)));
    ++groups;
}
static void close_lost_response(void) {
    fixture_t f; init(&f); open_client(&f, 0);
    assert(hb_muse_wire_client_prepare_command(&f.client, HB_MUSE_WIRE_CLOSE, 0, 0, NULL, 0, 1));
    uint8_t bytes[248], reply[248]; size_t length = request(&f, 1, bytes);
    server_reply(&f, bytes, length, 1, reply);
    assert(!f.server.active && f.server.max_epoch == 1);
    length = request(&f, 251, bytes);
    size_t reply_len = server_reply(&f, bytes, length, 251, reply);
    hb_muse_wire_frame_t out;
    assert(hb_muse_wire_client_accept_response(&f.client, reply, reply_len, 251, &out)
           == HB_MUSE_CLIENT_RESPONSE);
    assert(f.client.state == HB_MUSE_CLIENT_NEED_HELLO && !f.revokes);
    open_client(&f, 252); assert(f.client.epoch == 2);
    ++groups;
}
static void future_clock_without_overflow(void) {
    fixture_t f; init(&f); open_client(&f, UINT64_MAX - 1000u);
    command(&f, UINT64_MAX - 999u, HB_MUSE_WIRE_PCM);
    uint8_t bytes[248]; request(&f, UINT64_MAX - 999u, bytes);
    request(&f, UINT64_MAX - 749u, bytes);
    request(&f, UINT64_MAX - 499u, bytes);
    hb_muse_wire_client_tick(&f.client, UINT64_MAX - 249u);
    assert(f.client.fault == HB_MUSE_CLIENT_FAULT_TIMEOUT && !f.client.clock_fault);
    ++groups;
}
static void discovery_nonce_replay(void) {
    fixture_t f; init(&f);
    uint8_t bytes[248], old_hello[248], old_open[248], current[248];
    assert(hello(&f, 0));
    size_t length = request(&f, 0, bytes);
    size_t old_hello_len = server_reply(&f, bytes, length, 0, old_hello);
    hb_muse_wire_frame_t out;
    assert(hb_muse_wire_client_accept_response(&f.client, old_hello, old_hello_len, 0, &out)
           == HB_MUSE_CLIENT_RESPONSE);
    assert(hb_muse_wire_client_prepare_open(&f.client, 0));
    length = request(&f, 0, bytes);
    size_t old_open_len = server_reply(&f, bytes, length, 0, old_open);
    assert(hb_muse_wire_client_accept_response(&f.client, old_open, old_open_len, 0, &out)
           == HB_MUSE_CLIENT_RESPONSE);
    uint64_t old_nonce = f.client.discovery_nonce;
    hb_muse_wire_client_abandon(&f.client);
    assert(!hb_muse_wire_client_prepare_hello(&f.client, 0, 1));
    assert(!hb_muse_wire_client_prepare_hello(&f.client, old_nonce, 1));
    /* An entirely fresh P4 client still rejects prior discovery replies when
     * its platform supplies a new nonce, even with the same C5 boot ID. */
    assert(hb_muse_wire_client_init(&f.client, 0));
    hb_muse_wire_server_expire(&f.server, 2000);
    assert(hello(&f, 2000));
    assert(f.client.discovery_nonce != old_nonce);
    length = request(&f, 2000, bytes);
    size_t current_len = server_reply(&f, bytes, length, 2000, current);
    assert_ignored(&f, old_hello, old_hello_len, 2000);
    assert(f.client.pending && f.client.state == HB_MUSE_CLIENT_NEED_HELLO);
    assert(hb_muse_wire_client_accept_response(&f.client, current, current_len, 2000, &out)
           == HB_MUSE_CLIENT_RESPONSE);
    assert(hb_muse_wire_client_prepare_open(&f.client, 2000));
    length = request(&f, 2000, bytes);
    current_len = server_reply(&f, bytes, length, 2000, current);
    assert_ignored(&f, old_open, old_open_len, 2000);
    hb_muse_wire_frame_t wrong;
    assert(hb_muse_wire_decode(current, current_len, &wrong));
    wrong.payload[0] ^= 1u; /* Every header field matches; nonce must too. */
    size_t wrong_len = encode(&wrong, bytes);
    assert_ignored(&f, bytes, wrong_len, 2000);
    assert(f.client.pending && f.client.state == HB_MUSE_CLIENT_CAN_OPEN);
    assert(hb_muse_wire_client_accept_response(&f.client, current, current_len, 2000, &out)
           == HB_MUSE_CLIENT_RESPONSE);
    assert(f.client.state == HB_MUSE_CLIENT_ACTIVE && f.client.epoch == 2);
    ++groups;
}
static void replies_before_transmit_and_bad_open_shape(void) {
    fixture_t f; init(&f);
    assert(hello(&f, 0));
    uint8_t bytes[248], reply[248];
    size_t reply_len = server_reply(&f, f.client.encoded, f.client.encoded_len, 0, reply);
    assert_ignored(&f, reply, reply_len, 0); /* No actual send was recorded. */
    request(&f, 0, bytes);
    hb_muse_wire_frame_t out;
    assert(hb_muse_wire_client_accept_response(&f.client, reply, reply_len, 0, &out)
           == HB_MUSE_CLIENT_RESPONSE);
    assert(hb_muse_wire_client_prepare_open(&f.client, 0));
    size_t length = request(&f, 0, bytes);
    reply_len = server_reply(&f, bytes, length, 0, reply);
    hb_muse_wire_frame_t bad;
    assert(hb_muse_wire_decode(reply, reply_len, &bad));
    bad.payload_len = 0;
    size_t bad_len = encode(&bad, bytes);
    assert_ignored(&f, bytes, bad_len, 0);
    assert(hb_muse_wire_client_accept_response(&f.client, reply, reply_len, 0, &out)
           == HB_MUSE_CLIENT_RESPONSE);
    ++groups;
}
int main(void) {
    happy_path(); bounded_identical_retry(); lost_open_response(); timeout_no_new_audio();
    crc_and_foreign_never_expose(); fatal_statuses(); rejection_and_blocked();
    reboot_mid_command(); abandon_fences_then_reopens(); idle_lease_expiry();
    pending_unsent_lease_expiry(); rollback_permanent_until_init(); overflow_and_limits();
    no_concurrent_or_unprepared_send(); malformed_hello(); old_duplicate_ignored();
    maximum_payload_and_validation(); close_lost_response(); future_clock_without_overflow();
    discovery_nonce_replay(); replies_before_transmit_and_bad_open_shape();
    printf("Muse wire client: %u deterministic C11/UBSan test groups passed; no device/API.\n", groups);
    return 0;
}
