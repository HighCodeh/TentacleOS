/* Checked, headless Muse session seam. No radio/start/token operation here.
 * One serialized caller owns submit/audio/read/retire; the SDK worker owns
 * network state. QUEUED is local dispatch, not VM acceptance or a reply.
 * SDK startup/provisioning and High Boy drivers remain separate requirements.
 */
#ifndef HB_MUSE_SDK_PORT_H
#define HB_MUSE_SDK_PORT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define HB_MUSE_SDK_TEXT_CAP 1024u
#define HB_MUSE_SDK_MAX_FRAMES 240000u /* 15 seconds, PCM16 mono, 16 kHz */
typedef struct { uint32_t generation, turn; } hb_muse_sdk_id_t;
typedef enum {
    HB_MUSE_SDK_QUEUED, HB_MUSE_SDK_BLOCKED, HB_MUSE_SDK_REJECTED
} hb_muse_sdk_dispatch_t;
typedef enum {
    HB_MUSE_SDK_IDLE, HB_MUSE_SDK_BEGIN_PENDING, HB_MUSE_SDK_LISTENING,
    HB_MUSE_SDK_END_PENDING, HB_MUSE_SDK_WAIT_REPLY,
    HB_MUSE_SDK_CANCEL_PENDING, HB_MUSE_SDK_DONE, HB_MUSE_SDK_FAILED,
    HB_MUSE_SDK_FENCED
} hb_muse_sdk_phase_t;
typedef struct {
    hb_muse_sdk_id_t id;
    hb_muse_sdk_phase_t phase;
    bool worker_started, session_ready, begin_ack, end_ack, note_ack;
    uint32_t accepted_frames, text_revision, message_index;
    bool text_truncated;
    char text[HB_MUSE_SDK_TEXT_CAP]; /* latest real reply snapshot, not a log */
} hb_muse_sdk_snapshot_t;
typedef struct { size_t frames; bool failed; } hb_muse_sdk_audio_t;
typedef struct {
    uint32_t requested_revision, applied_revision;
    bool requested_enabled, enabled, fenced;
} hb_muse_sdk_session_snapshot_t;

/* Local session permission, separate from per-turn IDs. Disable revokes new
 * input immediately and is not queued behind PCM/commands. A nonzero return
 * identifies the requested revision; only the worker can acknowledge teardown.
 * Enable requires a completed disabled fence and an idle/retired turn. The
 * worker may finish an existing blocking TLS/account operation before fencing;
 * callers must not report closed based on timeout. Initial permission is off.
 */
uint32_t hb_muse_sdk_session_access(bool enabled);
bool hb_muse_sdk_session_snapshot(hb_muse_sdk_session_snapshot_t *out);

hb_muse_sdk_dispatch_t hb_muse_sdk_begin(hb_muse_sdk_id_t id);
hb_muse_sdk_audio_t hb_muse_sdk_audio(hb_muse_sdk_id_t id, uint32_t first_frame,
                                      const int16_t *pcm, size_t frames);
hb_muse_sdk_dispatch_t hb_muse_sdk_end(hb_muse_sdk_id_t id, uint32_t total_frames);
/* Stops accepting new PCM immediately, even if queue is full. Retry only
 * BLOCKED, with the same ID. FENCED means local teardown, NOT server erasure. */
hb_muse_sdk_dispatch_t hb_muse_sdk_cancel(hb_muse_sdk_id_t id);
bool hb_muse_sdk_snapshot(hb_muse_sdk_snapshot_t *out);
/* Only DONE/FENCED can retire. FAILED must be cancelled/fenced first. */
bool hb_muse_sdk_retire(hb_muse_sdk_id_t id);

#ifdef __cplusplus
}
#endif
#endif
