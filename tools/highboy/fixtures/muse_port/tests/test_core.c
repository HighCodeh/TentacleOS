#include "hb_muse_core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    hb_muse_core_t core;
    hb_muse_io_result_t begin_result, audio_result, end_result;
    unsigned begins, audios, ends, cancels;
    uint32_t last_first, last_total;
    size_t last_frames;
    hb_muse_turn_id_t last_id;
    int16_t copied[HB_MUSE_PCM_MAX_FRAMES];
} bench_t;

static unsigned passed;
static int16_t pcm[HB_MUSE_PCM_MAX_FRAMES];
static void pass(const char *name) { ++passed; printf("PASS %s\n", name); }
static bool same(hb_muse_turn_id_t a, hb_muse_turn_id_t b) {
    return a.generation == b.generation && a.turn == b.turn;
}
static hb_muse_io_result_t begin_cb(void *user, hb_muse_turn_id_t id) {
    bench_t *b = user; ++b->begins; b->last_id = id;
    assert(!hb_muse_capture_allowed(&b->core, id));
    return b->begin_result;
}
static hb_muse_io_result_t audio_cb(void *user, hb_muse_turn_id_t id,
                                   uint32_t first, const int16_t *samples, size_t n) {
    bench_t *b = user; ++b->audios;
    assert(hb_muse_capture_allowed(&b->core, id));
    b->last_id = id; b->last_first = first; b->last_frames = n;
    if (b->audio_result == HB_MUSE_IO_ACCEPTED) memcpy(b->copied, samples, n * sizeof(*samples));
    return b->audio_result;
}
static hb_muse_io_result_t end_cb(void *user, hb_muse_turn_id_t id, uint32_t total) {
    bench_t *b = user; ++b->ends; b->last_id = id; b->last_total = total;
    assert(!hb_muse_capture_allowed(&b->core, id));
    assert(!b->core.ptt_held && hb_muse_state(&b->core) == HB_MUSE_WAITING_REPLY);
    return b->end_result;
}
static void cancel_cb(void *user, hb_muse_turn_id_t id) {
    bench_t *b = user; ++b->cancels; b->last_id = id;
    assert(!hb_muse_capture_allowed(&b->core, id) && !b->core.active);
}
static const hb_muse_backend_t backend = {begin_cb, audio_cb, end_cb, cancel_cb};
static void fresh(bench_t *b) {
    memset(b, 0, sizeof(*b));
    assert(hb_muse_init(&b->core, &backend, b));
}
static void ready(bench_t *b) {
    fresh(b);
    assert(hb_muse_set_enabled(&b->core, true) == HB_MUSE_OK);
    assert(hb_muse_link_update(&b->core, hb_muse_generation(&b->core), true, true, true) == HB_MUSE_OK);
    assert(hb_muse_state(&b->core) == HB_MUSE_READY);
}
static hb_muse_turn_id_t start(bench_t *b) {
    assert(hb_muse_ptt(&b->core, true) == HB_MUSE_OK);
    hb_muse_turn_id_t id = {0};
    assert(hb_muse_active_turn(&b->core, &id));
    assert(hb_muse_capture_allowed(&b->core, id));
    return id;
}
static hb_muse_turn_id_t waiting(bench_t *b) {
    hb_muse_turn_id_t id = start(b);
    assert(hb_muse_offer_pcm(&b->core, id, 0, pcm, 320) == HB_MUSE_OK);
    assert(hb_muse_ptt(&b->core, false) == HB_MUSE_OK);
    assert(hb_muse_state(&b->core) == HB_MUSE_WAITING_REPLY);
    return id;
}

int main(void) {
    bench_t b;
    for (size_t i = 0; i < HB_MUSE_PCM_MAX_FRAMES; ++i) pcm[i] = (int16_t)((int)i - 160);
    fresh(&b);
    assert(hb_muse_state(&b.core) == HB_MUSE_DISABLED && hb_muse_generation(&b.core) == 1);
    assert(!b.begins && !b.audios && !b.ends && !b.cancels);
    pass("initialization is disabled offline and performs no backend operation");

    assert(!hb_muse_init(NULL, &backend, &b));
    assert(!hb_muse_init(&b.core, NULL, &b));
    assert(hb_muse_set_enabled(&b.core, true) == HB_MUSE_INVALID_ARGUMENT);
    for (unsigned i = 0; i < 4; ++i) {
        hb_muse_backend_t missing = backend;
        if (i == 0) missing.begin = NULL;
        if (i == 1) missing.audio = NULL;
        if (i == 2) missing.end = NULL;
        if (i == 3) missing.cancel = NULL;
        assert(!hb_muse_init(&b.core, &missing, &b));
    }
    pass("missing callbacks and invalid instance fail closed");

    fresh(&b);
    assert(hb_muse_link_update(&b.core, 1, true, true, true) == HB_MUSE_OK);
    assert(hb_muse_ptt(&b.core, true) == HB_MUSE_NOT_READY && !b.begins);
    assert(hb_muse_set_enabled(&b.core, true) == HB_MUSE_OK);
    assert(hb_muse_ptt(&b.core, true) == HB_MUSE_IGNORED && !b.begins);
    assert(hb_muse_ptt(&b.core, false) == HB_MUSE_IGNORED);
    (void)start(&b);
    pass("explicit feature enable while held cannot start until release and new physical press");

    fresh(&b); assert(hb_muse_set_enabled(&b.core, true) == HB_MUSE_OK);
    assert(hb_muse_ptt(&b.core, true) == HB_MUSE_NOT_READY);
    assert(hb_muse_link_update(&b.core, 1, true, true, true) == HB_MUSE_OK && !b.begins);
    assert(hb_muse_ptt(&b.core, true) == HB_MUSE_IGNORED);
    assert(hb_muse_ptt(&b.core, false) == HB_MUSE_IGNORED); (void)start(&b);
    pass("connection becoming ready never starts a held-button recording");

    const bool prerequisites_cases[][3] = {{false,false,false}, {true,false,false},
                                           {false,true,false}, {true,true,false}};
    for (size_t i = 0; i < sizeof(prerequisites_cases) / sizeof(prerequisites_cases[0]); ++i) {
        fresh(&b); assert(hb_muse_set_enabled(&b.core, true) == HB_MUSE_OK);
        assert(hb_muse_link_update(&b.core, 1, prerequisites_cases[i][0],
                    prerequisites_cases[i][1], prerequisites_cases[i][2]) == HB_MUSE_OK);
        assert(hb_muse_ptt(&b.core, true) == HB_MUSE_NOT_READY && !b.begins);
        assert(hb_muse_state(&b.core) == HB_MUSE_DISCONNECTED);
        pass("incomplete pairing network or actual SDK readiness cannot start capture");
    }
    fresh(&b); assert(hb_muse_link_update(&b.core, 1, false, true, true) == HB_MUSE_INVALID_ARGUMENT);
    assert(hb_muse_link_update(&b.core, hb_muse_generation(&b.core), true, false, true) == HB_MUSE_INVALID_ARGUMENT);
    assert(!b.core.transport_ready);
    pass("contradictory ready claims are rejected");

    ready(&b); hb_muse_turn_id_t invalidated = start(&b);
    assert(hb_muse_link_update(&b.core, 1, true, false, true) == HB_MUSE_INVALID_ARGUMENT);
    assert(b.cancels == 1 && !hb_muse_capture_allowed(&b.core, invalidated));
    assert(hb_muse_state(&b.core) == HB_MUSE_DISCONNECTED && hb_muse_generation(&b.core) == 2);
    pass("contradictory loss notification revokes an old capture grant instead of preserving it");

    ready(&b); hb_muse_turn_id_t id = start(&b);
    assert(hb_muse_ptt(&b.core, true) == HB_MUSE_IGNORED && b.begins == 1);
    assert(hb_muse_offer_pcm(&b.core, id, 0, pcm, 320) == HB_MUSE_OK);
    assert(b.audios == 1 && b.last_first == 0 && b.last_frames == 320);
    assert(!memcmp(b.copied, pcm, sizeof(pcm)) && b.core.accepted_frames == 320);
    assert(hb_muse_offer_pcm(&b.core, id, 320, pcm, 1) == HB_MUSE_OK);
    assert(hb_muse_ptt(&b.core, false) == HB_MUSE_OK && b.ends == 1 && b.last_total == 321);
    assert(hb_muse_backend_event(&b.core, id, HB_MUSE_EVENT_TURN_DONE) == HB_MUSE_OK);
    assert(hb_muse_state(&b.core) == HB_MUSE_READY && !b.cancels && !b.core.active);
    pass("fresh PTT ordered signed PCM release and exact backend completion form one turn");

    ready(&b); id = start(&b);
    assert(hb_muse_offer_pcm(&b.core, id, 1, pcm, 1) == HB_MUSE_OUT_OF_ORDER);
    assert(hb_muse_offer_pcm(&b.core, id, 0, pcm, 1) == HB_MUSE_OK);
    assert(hb_muse_offer_pcm(&b.core, id, 0, pcm, 1) == HB_MUSE_OUT_OF_ORDER && b.audios == 1);
    assert(hb_muse_offer_pcm(&b.core, id, 1, NULL, 1) == HB_MUSE_INVALID_ARGUMENT);
    assert(hb_muse_offer_pcm(&b.core, id, 1, pcm, 0) == HB_MUSE_INVALID_ARGUMENT);
    assert(hb_muse_offer_pcm(&b.core, id, 1, pcm, 321) == HB_MUSE_INVALID_ARGUMENT && b.audios == 1);
    pass("missing duplicate out-of-order empty and oversized audio never reaches backend");

    ready(&b); id = start(&b); b.audio_result = HB_MUSE_IO_WOULD_BLOCK;
    assert(hb_muse_offer_pcm(&b.core, id, 0, pcm, 320) == HB_MUSE_BACKPRESSURE);
    assert(!b.core.accepted_frames && b.audios == 1 && hb_muse_capture_allowed(&b.core, id));
    b.audio_result = HB_MUSE_IO_ACCEPTED;
    assert(hb_muse_offer_pcm(&b.core, id, 0, pcm, 320) == HB_MUSE_OK && b.core.accepted_frames == 320);
    pass("explicit non-consuming audio backpressure permits exact-offset caller retry");

    ready(&b); id = start(&b); b.audio_result = HB_MUSE_IO_WOULD_BLOCK;
    assert(hb_muse_offer_pcm(&b.core, id, 0, pcm, 320) == HB_MUSE_BACKPRESSURE);
    assert(hb_muse_ptt(&b.core, false) == HB_MUSE_OK && b.cancels == 1 && !b.ends);
    b.audio_result = HB_MUSE_IO_ACCEPTED;
    assert(hb_muse_offer_pcm(&b.core, id, 0, pcm, 320) == HB_MUSE_STALE && b.audios == 1);
    pass("release before queued acceptance discards pending audio and cancels empty turn");

    ready(&b); id = waiting(&b);
    assert(hb_muse_offer_pcm(&b.core, id, 320, pcm, 320) == HB_MUSE_NOT_READY && b.audios == 1);
    assert(hb_muse_ptt(&b.core, true) == HB_MUSE_NOT_READY && b.begins == 1);
    assert(hb_muse_backend_event(&b.core, id, HB_MUSE_EVENT_TURN_DONE) == HB_MUSE_OK);
    assert(hb_muse_ptt(&b.core, true) == HB_MUSE_IGNORED && b.begins == 1);
    pass("release prevents later PCM and reply completion cannot turn held input into new press");

    ready(&b); id = start(&b);
    assert(hb_muse_cancel(&b.core) == HB_MUSE_OK && b.cancels == 1);
    assert(hb_muse_cancel(&b.core) == HB_MUSE_IGNORED && b.cancels == 1);
    assert(hb_muse_offer_pcm(&b.core, id, 0, pcm, 320) == HB_MUSE_STALE && !b.audios);
    assert(hb_muse_ptt(&b.core, true) == HB_MUSE_IGNORED && b.begins == 1);
    assert(hb_muse_ptt(&b.core, false) == HB_MUSE_IGNORED);
    hb_muse_turn_id_t next = start(&b); assert(!same(id, next));
    assert(hb_muse_backend_event(&b.core, id, HB_MUSE_EVENT_TURN_DONE) == HB_MUSE_STALE);
    pass("cancel is idempotent old turn data is stale and a new turn needs a new edge");

    ready(&b); id = start(&b);
    assert(hb_muse_backend_event(&b.core, id, HB_MUSE_EVENT_TURN_DONE) == HB_MUSE_NOT_READY);
    assert(hb_muse_backend_event(&b.core, id, (hb_muse_event_t)99) == HB_MUSE_INVALID_ARGUMENT);
    assert(hb_muse_capture_allowed(&b.core, id));
    pass("early completion and unknown events cannot forge terminal or readiness state");

    assert(hb_muse_backend_event(&b.core, id, HB_MUSE_EVENT_TURN_FAILED) == HB_MUSE_BACKEND_ERROR);
    assert(b.cancels == 1 && !hb_muse_capture_allowed(&b.core, id));
    assert(hb_muse_offer_pcm(&b.core, id, 0, pcm, 320) == HB_MUSE_STALE && !b.audios);
    pass("actual backend failure while capturing immediately revokes further PCM");

    const bool lost_cases[][3] = {{false,true,false}, {true,false,false}, {true,true,false}};
    for (size_t i = 0; i < sizeof(lost_cases) / sizeof(lost_cases[0]); ++i) {
        ready(&b); id = start(&b); uint32_t old = hb_muse_generation(&b.core);
        assert(hb_muse_link_update(&b.core, old, lost_cases[i][0], lost_cases[i][1], lost_cases[i][2]) == HB_MUSE_OK);
        assert(hb_muse_generation(&b.core) == old + 1 && b.cancels == 1);
        assert(hb_muse_state(&b.core) == HB_MUSE_DISCONNECTED && !hb_muse_capture_allowed(&b.core, id));
        assert(hb_muse_link_update(&b.core, old, true, true, true) == HB_MUSE_STALE);
        assert(hb_muse_backend_event(&b.core, id, HB_MUSE_EVENT_TURN_DONE) == HB_MUSE_STALE);
        assert(hb_muse_offer_pcm(&b.core, id, 0, pcm, 320) == HB_MUSE_STALE && !b.audios);
        pass("loss of any prerequisite revokes capture and invalidates stale ready audio and done events");
    }
    ready(&b); id = waiting(&b); uint32_t generation = hb_muse_generation(&b.core);
    assert(hb_muse_connection_reset(&b.core) == HB_MUSE_OK && b.cancels == 1);
    assert(hb_muse_link_update(&b.core, generation, true, true, true) == HB_MUSE_STALE);
    assert(hb_muse_backend_event(&b.core, id, HB_MUSE_EVENT_TURN_DONE) == HB_MUSE_STALE);
    assert(hb_muse_state(&b.core) == HB_MUSE_DISCONNECTED);
    pass("connection replacement discards pending reply and old readiness");

    ready(&b); id = start(&b); generation = hb_muse_generation(&b.core);
    assert(hb_muse_set_enabled(&b.core, false) == HB_MUSE_OK && b.cancels == 1);
    assert(hb_muse_state(&b.core) == HB_MUSE_DISABLED);
    assert(hb_muse_set_enabled(&b.core, true) == HB_MUSE_OK);
    assert(hb_muse_link_update(&b.core, generation, true, true, true) == HB_MUSE_STALE);
    assert(hb_muse_link_update(&b.core, hb_muse_generation(&b.core), true, true, true) == HB_MUSE_OK);
    assert(hb_muse_ptt(&b.core, true) == HB_MUSE_IGNORED && b.begins == 1);
    assert(hb_muse_offer_pcm(&b.core, id, 0, pcm, 320) == HB_MUSE_STALE);
    pass("disabling immediately revokes active turn and enabling cannot reuse credentials epoch or held press");

    ready(&b); b.begin_result = HB_MUSE_IO_WOULD_BLOCK;
    assert(hb_muse_ptt(&b.core, true) == HB_MUSE_BACKPRESSURE && !b.cancels && !b.core.active);
    assert(hb_muse_ptt(&b.core, true) == HB_MUSE_IGNORED && b.begins == 1);
    assert(hb_muse_ptt(&b.core, false) == HB_MUSE_IGNORED);
    b.begin_result = HB_MUSE_IO_ACCEPTED; id = start(&b); assert(id.turn == 2);
    pass("blocked begin is never automatically retried and its local ID is not reused");

    ready(&b); b.begin_result = HB_MUSE_IO_FAILED;
    assert(hb_muse_ptt(&b.core, true) == HB_MUSE_BACKEND_ERROR && b.cancels == 1);
    assert(!b.core.active && hb_muse_state(&b.core) == HB_MUSE_ERROR);
    pass("ambiguous begin failure cancels without recording or retry");
    ready(&b); id = start(&b); b.audio_result = HB_MUSE_IO_FAILED;
    assert(hb_muse_offer_pcm(&b.core, id, 0, pcm, 320) == HB_MUSE_BACKEND_ERROR && b.cancels == 1);
    assert(hb_muse_offer_pcm(&b.core, id, 0, pcm, 320) == HB_MUSE_STALE && b.audios == 1);
    pass("ambiguous audio failure cancels and cannot duplicate an uncertain chunk");
    const hb_muse_io_result_t end_errors[] = {HB_MUSE_IO_WOULD_BLOCK, HB_MUSE_IO_FAILED};
    for (size_t i = 0; i < sizeof(end_errors) / sizeof(end_errors[0]); ++i) {
        ready(&b); id = start(&b);
        assert(hb_muse_offer_pcm(&b.core, id, 0, pcm, 320) == HB_MUSE_OK);
        b.end_result = end_errors[i];
        hb_muse_result_t result = hb_muse_ptt(&b.core, false);
        assert(result == (i ? HB_MUSE_BACKEND_ERROR : HB_MUSE_BACKPRESSURE));
        assert(b.ends == 1 && b.cancels == 1 && !b.core.active);
        assert(hb_muse_ptt(&b.core, false) == HB_MUSE_IGNORED && b.ends == 1);
        pass("unaccepted end fails closed rather than sending an automatic duplicate commit");
    }
    ready(&b); id = waiting(&b);
    assert(hb_muse_backend_event(&b.core, id, HB_MUSE_EVENT_TURN_FAILED) == HB_MUSE_BACKEND_ERROR);
    assert(b.cancels == 1 && hb_muse_state(&b.core) == HB_MUSE_ERROR);
    pass("backend failure event terminates exact waiting turn without fake success");

    ready(&b); b.core.next_turn = UINT32_MAX;
    assert(hb_muse_ptt(&b.core, true) == HB_MUSE_ID_EXHAUSTED && !b.begins);
    assert(hb_muse_state(&b.core) == HB_MUSE_ERROR && !b.core.enabled);
    pass("turn sequence exhaustion fails closed rather than reusing an ID");
    ready(&b); id = start(&b); b.core.generation = UINT32_MAX;
    assert(hb_muse_connection_reset(&b.core) == HB_MUSE_ID_EXHAUSTED && b.cancels == 1);
    assert(hb_muse_generation(&b.core) == UINT32_MAX && !b.core.enabled);
    assert(hb_muse_link_update(&b.core, UINT32_MAX, true, true, true) == HB_MUSE_ID_EXHAUSTED);
    pass("connection generation exhaustion permanently fails closed for this instance");
    ready(&b); id = start(&b); b.core.accepted_frames = UINT32_MAX - 1;
    assert(hb_muse_offer_pcm(&b.core, id, UINT32_MAX - 1, pcm, 2) == HB_MUSE_ID_EXHAUSTED);
    assert(!b.audios && b.cancels == 1 && !b.core.active);
    pass("sample counter cannot wrap and admit duplicate audio");

    printf("%u portable core scenarios passed; no SDK, hardware, microphone, network or API used\n", passed);
    return 0;
}
