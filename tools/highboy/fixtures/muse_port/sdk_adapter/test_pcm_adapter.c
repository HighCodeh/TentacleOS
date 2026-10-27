/* Explicit fake transport for fault injection. These tests call the REAL
 * portable adapter and controller; no SDK/network/hardware success is implied. */
#include "hb_muse_pcm_adapter.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    hb_muse_pcm_adapter_t adapter;
    hb_muse_core_t core;
    hb_muse_turn_id_t id;
    hb_muse_dispatch_t begin_result, end_result, cancel_result;
    hb_muse_audio_write_t write_result;
    bool automatic_write;
    size_t max_prefix;
    unsigned begins, writes, ends, cancels;
    uint32_t total, last_first;
    size_t offered, received;
    int16_t samples[8192];
} fixture_t;

static const hb_muse_backend_t *b;
static unsigned tests;
static bool id_equal(hb_muse_turn_id_t a, hb_muse_turn_id_t c) {
    return a.generation == c.generation && a.turn == c.turn;
}
static hb_muse_dispatch_t fake_begin(void *user, hb_muse_turn_id_t id) {
    fixture_t *f = user;
    assert(id_equal(id, f->id));
    ++f->begins;
    return f->begin_result;
}
static hb_muse_audio_write_t fake_audio(void *user, hb_muse_turn_id_t id,
                                       uint32_t first, const int16_t *pcm, size_t frames) {
    fixture_t *f = user;
    assert(id_equal(id, f->id));
    assert(first == f->received);
    assert(frames > 0 && frames <= HB_MUSE_PCM_MAX_FRAMES);
    ++f->writes;
    f->last_first = first;
    f->offered = frames;
    hb_muse_audio_write_t r = f->write_result;
    if (f->automatic_write) {
        r.status = HB_MUSE_AUDIO_PROGRESS;
        r.frames = frames < f->max_prefix ? frames : f->max_prefix;
    }
    /* Fault responses may lie about their count: never dereference beyond the
     * offer in the test harness, so sanitizers validate the adapter itself. */
    if (r.status == HB_MUSE_AUDIO_PROGRESS && r.frames <= frames) {
        assert(f->received + r.frames <= sizeof(f->samples) / sizeof(f->samples[0]));
        memcpy(f->samples + f->received, pcm, r.frames * sizeof(*pcm));
        f->received += r.frames;
    }
    return r;
}
static hb_muse_dispatch_t fake_end(void *user, hb_muse_turn_id_t id, uint32_t total) {
    fixture_t *f = user;
    assert(id_equal(id, f->id));
    assert(total == f->received); /* End must never overtake accepted PCM. */
    ++f->ends;
    f->total = total;
    return f->end_result;
}
static hb_muse_dispatch_t fake_cancel(void *user, hb_muse_turn_id_t id) {
    fixture_t *f = user;
    assert(id_equal(id, f->id));
    assert(f->adapter.queued == 0);
    for (size_t i = 0; i < HB_MUSE_ADAPTER_CAPACITY; ++i) assert(f->adapter.pcm[i] == 0);
    ++f->cancels;
    return f->cancel_result;
}
static const hb_muse_adapter_transport_t TRANSPORT = {fake_begin, fake_audio, fake_end, fake_cancel};

static void setup(fixture_t *f) {
    memset(f, 0, sizeof(*f));
    f->begin_result = f->end_result = f->cancel_result = HB_MUSE_DISPATCH_QUEUED;
    f->automatic_write = true;
    f->max_prefix = HB_MUSE_PCM_MAX_FRAMES;
    assert(hb_muse_pcm_adapter_init(&f->adapter, &TRANSPORT, f));
    assert(hb_muse_init(&f->core, b, &f->adapter));
    assert(hb_muse_set_enabled(&f->core, true) == HB_MUSE_OK);
    assert(hb_muse_link_update(&f->core, hb_muse_generation(&f->core), true, true, true) == HB_MUSE_OK);
}
static void press(fixture_t *f) {
    assert(hb_muse_ptt(&f->core, true) == HB_MUSE_OK);
    assert(hb_muse_active_turn(&f->core, &f->id));
    assert(f->adapter.phase == HB_MUSE_ADAPTER_BEGIN_PENDING);
}
static void started(fixture_t *f) {
    press(f);
    hb_muse_pcm_adapter_pump(&f->adapter);
    assert(f->adapter.phase == HB_MUSE_ADAPTER_BEGIN_WAIT);
    assert(hb_muse_pcm_adapter_event(&f->adapter, f->id, HB_MUSE_ADAPTER_BEGIN_ACK));
}
static void data(int16_t *pcm, size_t frames, uint32_t first) {
    for (size_t i = 0; i < frames; ++i) pcm[i] = (int16_t)((first + (uint32_t)i) % 30000u + 1u);
}
static hb_muse_result_t offer(fixture_t *f, size_t frames) {
    int16_t pcm[HB_MUSE_PCM_MAX_FRAMES];
    data(pcm, frames, f->core.accepted_frames);
    return hb_muse_offer_pcm(&f->core, f->id, f->core.accepted_frames, pcm, frames);
}
static void verify_sequence(fixture_t *f) {
    for (size_t i = 0; i < f->received; ++i) assert(f->samples[i] == (int16_t)(i % 30000u + 1u));
}
static void drain(fixture_t *f) {
    for (unsigned budget = 0; f->adapter.queued; ++budget) {
        assert(budget < HB_MUSE_ADAPTER_CAPACITY + 1);
        hb_muse_pcm_adapter_pump(&f->adapter);
    }
}
static void deliver(fixture_t *f, hb_muse_event_t wanted) {
    hb_muse_turn_id_t id;
    hb_muse_event_t event;
    assert(hb_muse_pcm_adapter_take_event(&f->adapter, &id, &event));
    assert(id_equal(id, f->id) && event == wanted);
    assert(hb_muse_backend_event(&f->core, id, event) ==
           (wanted == HB_MUSE_EVENT_TURN_DONE ? HB_MUSE_OK : HB_MUSE_BACKEND_ERROR));
    assert(!hb_muse_pcm_adapter_take_event(&f->adapter, &id, &event));
}
static void release_and_drain(fixture_t *f) {
    assert(hb_muse_ptt(&f->core, false) == HB_MUSE_OK);
    assert(!hb_muse_capture_allowed(&f->core, f->id));
    drain(f);
    assert(f->ends == 0);
    hb_muse_pcm_adapter_pump(&f->adapter);
    assert(f->adapter.phase == HB_MUSE_ADAPTER_END_WAIT);
}

static void test_init(void) {
    hb_muse_pcm_adapter_t a;
    assert(!hb_muse_pcm_adapter_init(NULL, &TRANSPORT, NULL));
    assert(!hb_muse_pcm_adapter_init(&a, NULL, NULL));
    hb_muse_adapter_transport_t ops = TRANSPORT;
    ops.audio = NULL;
    assert(!hb_muse_pcm_adapter_init(&a, &ops, NULL));
    hb_muse_pcm_adapter_pump(NULL);
    hb_muse_pcm_adapter_pump(&a);
    assert(!hb_muse_pcm_adapter_event(NULL, (hb_muse_turn_id_t){1,1}, HB_MUSE_ADAPTER_BEGIN_ACK));
    assert(b->begin(&a, (hb_muse_turn_id_t){1,1}) == HB_MUSE_IO_FAILED);
}
static void test_explicit_begin_ack(void) {
    fixture_t f; setup(&f); press(&f);
    assert(offer(&f, 320) == HB_MUSE_OK);
    assert(f.begins == 0 && f.writes == 0);
    assert(!hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_BEGIN_ACK));
    hb_muse_pcm_adapter_pump(&f.adapter);
    for (int i = 0; i < 8; ++i) hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.begins == 1 && f.writes == 0 && f.adapter.queued == 320);
    assert(hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_BEGIN_ACK));
    assert(!hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_BEGIN_ACK));
    hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.received == 320);
}
static void test_whole_copy_and_partial_prefix(void) {
    fixture_t f; setup(&f); started(&f);
    int16_t pcm[320]; data(pcm, 320, 0);
    assert(hb_muse_offer_pcm(&f.core, f.id, 0, pcm, 320) == HB_MUSE_OK);
    memset(pcm, 0, sizeof(pcm));
    f.max_prefix = 73;
    hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.received == 73 && f.adapter.queued == 247 && f.adapter.consumed_frames == 73);
    hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.last_first == 73 && f.offered == 247);
    drain(&f);
    assert(f.received == 320 && f.writes == 5);
    verify_sequence(&f);
}
static void test_backpressure_all_or_none(void) {
    fixture_t f; setup(&f); started(&f);
    for (int i = 0; i < 4; ++i) assert(offer(&f, 320) == HB_MUSE_OK);
    hb_muse_pcm_adapter_t snapshot = f.adapter;
    assert(offer(&f, 1) == HB_MUSE_BACKPRESSURE);
    assert(memcmp(&snapshot, &f.adapter, sizeof(snapshot)) == 0);
    assert(f.core.accepted_frames == 1280 && f.adapter.accepted_frames == 1280);
    f.max_prefix = 17;
    hb_muse_pcm_adapter_pump(&f.adapter);
    snapshot = f.adapter;
    assert(offer(&f, 18) == HB_MUSE_BACKPRESSURE);
    assert(memcmp(&snapshot, &f.adapter, sizeof(snapshot)) == 0);
    assert(offer(&f, 17) == HB_MUSE_OK);
    drain(&f);
    assert(f.received == 1297);
    verify_sequence(&f);
}
static void test_ring_wrap_and_every_small_prefix(void) {
    for (size_t prefix = 1; prefix <= 321; prefix += 16) {
        fixture_t f; setup(&f); started(&f); f.max_prefix = prefix;
        for (unsigned i = 0; i < 20; ++i) {
            while (offer(&f, 257) == HB_MUSE_BACKPRESSURE) hb_muse_pcm_adapter_pump(&f.adapter);
            hb_muse_pcm_adapter_pump(&f.adapter);
        }
        drain(&f);
        assert(f.received == 5140 && f.adapter.consumed_frames == 5140);
        verify_sequence(&f);
    }
}
static void test_release_drain_then_async_done(void) {
    fixture_t f; setup(&f); started(&f);
    assert(offer(&f, 320) == HB_MUSE_OK);
    assert(offer(&f, 17) == HB_MUSE_OK);
    f.max_prefix = 29;
    release_and_drain(&f);
    assert(f.total == 337 && f.ends == 1);
    assert(!hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_TURN_DONE));
    assert(hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_END_ACK));
    for (int i = 0; i < 5; ++i) hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.ends == 1 && !f.adapter.event_pending);
    assert(hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_TURN_DONE));
    assert(b->begin(&f.adapter, (hb_muse_turn_id_t){f.id.generation, f.id.turn+1}) == HB_MUSE_IO_WOULD_BLOCK);
    deliver(&f, HB_MUSE_EVENT_TURN_DONE);
    assert(hb_muse_state(&f.core) == HB_MUSE_READY);
    assert(!hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_TURN_DONE));
    verify_sequence(&f);
}
static void test_release_before_begin_ack(void) {
    fixture_t f; setup(&f); press(&f);
    assert(offer(&f, 101) == HB_MUSE_OK);
    assert(hb_muse_ptt(&f.core, false) == HB_MUSE_OK);
    assert(offer(&f, 1) == HB_MUSE_NOT_READY);
    int16_t one = 7;
    assert(b->audio(&f.adapter, f.id, 101, &one, 1) == HB_MUSE_IO_FAILED);
    hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.writes == 0 && f.ends == 0);
    assert(hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_BEGIN_ACK));
    drain(&f); hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.total == 101);
}
static void test_cancel_before_dispatch_is_local(void) {
    fixture_t f; setup(&f); press(&f);
    assert(offer(&f, 320) == HB_MUSE_OK);
    assert(hb_muse_cancel(&f.core) == HB_MUSE_OK);
    hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.begins == 0 && f.writes == 0 && f.cancels == 0);
    assert(f.adapter.phase == HB_MUSE_ADAPTER_IDLE && !f.adapter.event_pending);
    for (size_t i = 0; i < HB_MUSE_ADAPTER_CAPACITY; ++i) assert(f.adapter.pcm[i] == 0);
    assert(!hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_BEGIN_ACK));
    assert(b->begin(&f.adapter, f.id) == HB_MUSE_IO_FAILED);
}
static void test_cancel_revokes_unsent_suffix_and_fences(void) {
    fixture_t f; setup(&f); started(&f);
    assert(offer(&f, 320) == HB_MUSE_OK);
    f.max_prefix = 41;
    hb_muse_pcm_adapter_pump(&f.adapter);
    assert(hb_muse_cancel(&f.core) == HB_MUSE_OK);
    assert(f.adapter.queued == 0 && f.adapter.phase == HB_MUSE_ADAPTER_CANCEL_PENDING);
    hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.cancels == 1 && f.adapter.phase == HB_MUSE_ADAPTER_CANCEL_WAIT);
    for (int i = 0; i < 10; ++i) hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.received == 41 && f.writes == 1 && f.cancels == 1);
    assert(b->begin(&f.adapter, (hb_muse_turn_id_t){f.id.generation, f.id.turn+1}) == HB_MUSE_IO_WOULD_BLOCK);
    assert(!hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_TURN_DONE));
    assert(hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_FENCED));
    assert(!f.adapter.event_pending);
    assert(hb_muse_ptt(&f.core, false) == HB_MUSE_IGNORED);
    press(&f);
    assert(f.id.turn == 2);
}
static void test_late_stale_events_and_pcm(void) {
    fixture_t f; setup(&f); started(&f);
    hb_muse_turn_id_t stale = {f.id.generation + 1, f.id.turn};
    hb_muse_pcm_adapter_t before = f.adapter;
    int16_t pcm = 3;
    for (int ev = HB_MUSE_ADAPTER_BEGIN_ACK; ev <= HB_MUSE_ADAPTER_FENCED; ++ev) {
        assert(!hb_muse_pcm_adapter_event(&f.adapter, stale, (hb_muse_adapter_event_t)ev));
        assert(memcmp(&before, &f.adapter, sizeof(before)) == 0);
    }
    assert(b->audio(&f.adapter, stale, 0, &pcm, 1) == HB_MUSE_IO_FAILED);
    b->cancel(&f.adapter, stale);
    assert(memcmp(&before, &f.adapter, sizeof(before)) == 0);
}
static void test_audio_blocked_then_progress(void) {
    fixture_t f; setup(&f); started(&f); assert(offer(&f, 320) == HB_MUSE_OK);
    f.automatic_write = false;
    f.write_result = (hb_muse_audio_write_t){HB_MUSE_AUDIO_BLOCKED, 0};
    for (int i = 0; i < 7; ++i) hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.adapter.queued == 320 && f.adapter.consumed_frames == 0 && f.received == 0);
    f.automatic_write = true; drain(&f);
    assert(f.received == 320); verify_sequence(&f);
}
static void test_invalid_or_uncertain_audio_never_replays(void) {
    hb_muse_audio_write_t bad[] = {{HB_MUSE_AUDIO_FAILED,0}, {HB_MUSE_AUDIO_FAILED,13},
        {HB_MUSE_AUDIO_PROGRESS,0}, {HB_MUSE_AUDIO_PROGRESS,321},
        {HB_MUSE_AUDIO_BLOCKED,1}, {(hb_muse_audio_status_t)99,0}};
    for (size_t i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        fixture_t f; setup(&f); started(&f); assert(offer(&f, 320) == HB_MUSE_OK);
        f.automatic_write = false; f.write_result = bad[i];
        hb_muse_pcm_adapter_pump(&f.adapter);
        assert(f.adapter.queued == 0 && f.adapter.phase == HB_MUSE_ADAPTER_CANCEL_PENDING);
        deliver(&f, HB_MUSE_EVENT_TURN_FAILED);
        for (int j = 0; j < 5; ++j) hb_muse_pcm_adapter_pump(&f.adapter);
        assert(f.writes == 1 && f.cancels == 1 && f.ends == 0);
        assert(hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_FENCED));
    }
}
static void test_begin_blocked_and_failed(void) {
    fixture_t f; setup(&f); press(&f); f.begin_result = HB_MUSE_DISPATCH_BLOCKED;
    assert(offer(&f, 320) == HB_MUSE_OK);
    hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.adapter.phase == HB_MUSE_ADAPTER_BEGIN_PENDING && !f.adapter.transport_touched);
    f.begin_result = HB_MUSE_DISPATCH_FAILED;
    hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.adapter.phase == HB_MUSE_ADAPTER_CANCEL_PENDING && f.adapter.queued == 0);
    deliver(&f, HB_MUSE_EVENT_TURN_FAILED);
    for (int j = 0; j < 5; ++j) hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.begins == 2 && f.writes == 0 && f.cancels == 1);
}
static void test_end_blocked_and_failed(void) {
    fixture_t f; setup(&f); started(&f); assert(offer(&f, 13) == HB_MUSE_OK);
    assert(hb_muse_ptt(&f.core, false) == HB_MUSE_OK); drain(&f);
    f.end_result = HB_MUSE_DISPATCH_BLOCKED;
    hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.adapter.phase == HB_MUSE_ADAPTER_STREAM && f.ends == 1);
    f.end_result = HB_MUSE_DISPATCH_FAILED;
    hb_muse_pcm_adapter_pump(&f.adapter);
    deliver(&f, HB_MUSE_EVENT_TURN_FAILED);
    for (int j = 0; j < 5; ++j) hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.ends == 2 && f.cancels == 1);
}
static void test_cancel_uncertain_requires_fence_no_retry(void) {
    fixture_t f; setup(&f); started(&f); assert(offer(&f, 13) == HB_MUSE_OK);
    assert(hb_muse_cancel(&f.core) == HB_MUSE_OK);
    f.cancel_result = HB_MUSE_DISPATCH_BLOCKED;
    hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.adapter.phase == HB_MUSE_ADAPTER_CANCEL_PENDING);
    f.cancel_result = HB_MUSE_DISPATCH_FAILED;
    hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.adapter.phase == HB_MUSE_ADAPTER_FENCE_REQUIRED);
    for (int i = 0; i < 5; ++i) hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.cancels == 2 && f.received == 0 && f.writes == 0);
    assert(hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_FENCED));
}
static void test_verified_teardown_fences_before_cancel_dispatch(void) {
    fixture_t f; setup(&f); started(&f); assert(offer(&f, 13) == HB_MUSE_OK);
    assert(hb_muse_cancel(&f.core) == HB_MUSE_OK);
    assert(f.adapter.phase == HB_MUSE_ADAPTER_CANCEL_PENDING);
    /* Binding reports actual teardown/drain, not merely connection loss. */
    assert(hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_FENCED));
    hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.cancels == 0 && f.writes == 0);
    assert(hb_muse_ptt(&f.core, false) == HB_MUSE_IGNORED);
    press(&f);
    assert(f.id.turn == 2);
}
static void test_disconnect_core_revokes_queue_and_old_generation(void) {
    fixture_t f; setup(&f); started(&f); assert(offer(&f, 320) == HB_MUSE_OK);
    uint32_t old_generation = f.id.generation;
    assert(hb_muse_link_update(&f.core, old_generation, true, false, false) == HB_MUSE_OK);
    assert(!hb_muse_capture_allowed(&f.core, f.id) && f.adapter.queued == 0);
    assert(hb_muse_generation(&f.core) == old_generation + 1);
    hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.writes == 0 && f.cancels == 1);
    assert(hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_FENCED));
    assert(hb_muse_link_update(&f.core, hb_muse_generation(&f.core), true, true, true) == HB_MUSE_OK);
    assert(hb_muse_ptt(&f.core, true) == HB_MUSE_IGNORED);
    assert(hb_muse_ptt(&f.core, false) == HB_MUSE_IGNORED);
    hb_muse_turn_id_t old = f.id; press(&f);
    assert(f.id.generation > old.generation);
    assert(!hb_muse_pcm_adapter_event(&f.adapter, old, HB_MUSE_ADAPTER_BEGIN_ACK));
}
static void test_deadline_fault_drops_capture(void) {
    fixture_t f; setup(&f); press(&f); assert(offer(&f, 320) == HB_MUSE_OK);
    hb_muse_pcm_adapter_pump(&f.adapter);
    assert(hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_TURN_FAILED));
    assert(f.adapter.queued == 0);
    deliver(&f, HB_MUSE_EVENT_TURN_FAILED);
    assert(!hb_muse_capture_allowed(&f.core, f.id));
    assert(!hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_BEGIN_ACK));
}
static void test_direct_bad_inputs_leave_ring_intact(void) {
    fixture_t f; setup(&f); started(&f); assert(offer(&f, 2) == HB_MUSE_OK);
    int16_t pcm[321] = {0};
    hb_muse_pcm_adapter_t before = f.adapter;
    assert(b->audio(&f.adapter, f.id, 2, pcm, 321) == HB_MUSE_IO_FAILED);
    assert(b->audio(&f.adapter, f.id, 2, NULL, 1) == HB_MUSE_IO_FAILED);
    assert(b->audio(&f.adapter, f.id, 2, pcm, 0) == HB_MUSE_IO_FAILED);
    assert(b->audio(&f.adapter, f.id, 0, pcm, 2) == HB_MUSE_IO_FAILED);
    assert(b->end(&f.adapter, f.id, 1) == HB_MUSE_IO_FAILED);
    assert(b->end(&f.adapter, f.id, 0) == HB_MUSE_IO_FAILED);
    assert(!hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_END_ACK));
    assert(!hb_muse_pcm_adapter_event(&f.adapter, f.id, HB_MUSE_ADAPTER_TURN_DONE));
    assert(!hb_muse_pcm_adapter_event(&f.adapter, f.id, (hb_muse_adapter_event_t)99));
    assert(memcmp(&before, &f.adapter, sizeof(before)) == 0);
}
static void test_counter_overflow_rejected(void) {
    fixture_t f; setup(&f); started(&f);
    f.adapter.accepted_frames = UINT32_MAX;
    int16_t sample = 1;
    assert(b->audio(&f.adapter, f.id, UINT32_MAX, &sample, 1) == HB_MUSE_IO_FAILED);
    assert(f.adapter.queued == 0 && f.adapter.accepted_frames == UINT32_MAX);
}
static void test_empty_tap_never_dispatches(void) {
    fixture_t f; setup(&f); press(&f);
    assert(hb_muse_ptt(&f.core, false) == HB_MUSE_OK);
    hb_muse_pcm_adapter_pump(&f.adapter);
    assert(f.begins == 0 && f.ends == 0 && f.cancels == 0);
    assert(f.adapter.phase == HB_MUSE_ADAPTER_IDLE);
}

#define RUN(fn) do { fn(); ++tests; puts(#fn " PASS"); } while (0)
int main(void) {
    b = hb_muse_pcm_adapter_backend();
    RUN(test_init);
    RUN(test_explicit_begin_ack);
    RUN(test_whole_copy_and_partial_prefix);
    RUN(test_backpressure_all_or_none);
    RUN(test_ring_wrap_and_every_small_prefix);
    RUN(test_release_drain_then_async_done);
    RUN(test_release_before_begin_ack);
    RUN(test_cancel_before_dispatch_is_local);
    RUN(test_cancel_revokes_unsent_suffix_and_fences);
    RUN(test_late_stale_events_and_pcm);
    RUN(test_audio_blocked_then_progress);
    RUN(test_invalid_or_uncertain_audio_never_replays);
    RUN(test_begin_blocked_and_failed);
    RUN(test_end_blocked_and_failed);
    RUN(test_cancel_uncertain_requires_fence_no_retry);
    RUN(test_verified_teardown_fences_before_cancel_dispatch);
    RUN(test_disconnect_core_revokes_queue_and_old_generation);
    RUN(test_deadline_fault_drops_capture);
    RUN(test_direct_bad_inputs_leave_ring_intact);
    RUN(test_counter_overflow_rejected);
    RUN(test_empty_tap_never_dispatches);
    printf("%u PCM adapter test groups passed; no hardware/SDK/API used.\n", tests);
    return 0;
}
