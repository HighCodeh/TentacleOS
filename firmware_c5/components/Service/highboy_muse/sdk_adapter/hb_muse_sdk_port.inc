/* Included only by the isolated SDK's muse_chat_session.cpp after its worker
 * helpers. Direct binding to real queues/streams/turn functions, not a mock.
 * The host harness compiles this same file against explicit RTOS/VM fakes.
 * Mutex sections contain only bounded data copies; no RTOS/network calls.
 */
static portMUX_TYPE s_port_lock = portMUX_INITIALIZER_UNLOCKED;
static hb_muse_sdk_snapshot_t s_port = {};
static hb_muse_sdk_id_t s_port_last = {};
static bool s_port_cancel_queued;
static bool s_port_subscribed; /* worker-only; true on subscription HTTP 2xx */
static hb_muse_sdk_session_snapshot_t s_port_session = {0, 0, false, false, true};

static bool port_session_allowed_locked(void)
{
    return s_port_session.requested_enabled && s_port_session.enabled &&
        s_port_session.requested_revision == s_port_session.applied_revision;
}
extern "C" bool hb_muse_sdk_session_snapshot(hb_muse_sdk_session_snapshot_t *out)
{
    if (!out) return false;
    portENTER_CRITICAL(&s_port_lock);
    *out = s_port_session;
    portEXIT_CRITICAL(&s_port_lock);
    return true;
}
extern "C" uint32_t hb_muse_sdk_session_access(bool enabled)
{
    portENTER_CRITICAL(&s_port_lock);
    if (s_port_session.requested_revision && enabled == s_port_session.requested_enabled) {
        uint32_t revision = s_port_session.requested_revision;
        portEXIT_CRITICAL(&s_port_lock);
        return revision;
    }
    if ((enabled && (s_port_session.requested_revision >= UINT32_MAX - 1u ||
        !s_port_session.fenced || s_port.phase != HB_MUSE_SDK_IDLE ||
        s_port_session.requested_revision != s_port_session.applied_revision)) ||
        s_port_session.requested_revision == UINT32_MAX) {
        portEXIT_CRITICAL(&s_port_lock);
        return 0;
    }
    uint32_t revision = ++s_port_session.requested_revision;
    s_port_session.requested_enabled = enabled;
    s_port_session.enabled = false;
    s_port_session.fenced = false;
    s_port.session_ready = false;
    if (!enabled && s_port.id.generation) s_port.phase = HB_MUSE_SDK_CANCEL_PENDING;
    portEXIT_CRITICAL(&s_port_lock);
    return revision;
}
/* Worker-only checkpoint: no queue-full failure and no fabricated fence. */
static bool port_session_checkpoint(void)
{
    portENTER_CRITICAL(&s_port_lock);
    uint32_t revision = s_port_session.requested_revision;
    bool enabled = s_port_session.requested_enabled;
    bool pending = revision != s_port_session.applied_revision;
    bool allowed = port_session_allowed_locked();
    portEXIT_CRITICAL(&s_port_lock);
    if (!pending) return allowed;
    if (!enabled) {
        turn_finish();
        disconnect("local session disabled");
        forget_vm();
        if (s_in) xStreamBufferReset(s_in);
        drain_out();
        if (s_events) xQueueReset(s_events);
        s_auto_next_us = 0;
    }
    portENTER_CRITICAL(&s_port_lock);
    if (revision == s_port_session.requested_revision) {
        s_port_session.applied_revision = revision;
        s_port_session.enabled = enabled;
        s_port_session.fenced = !enabled;
        if (!enabled) {
            s_port.session_ready = false;
            if (s_port.id.generation) s_port.phase = HB_MUSE_SDK_FENCED;
            s_port.text[0] = '\0';
        }
    }
    allowed = port_session_allowed_locked();
    portEXIT_CRITICAL(&s_port_lock);
    return allowed;
}

static bool port_equal(hb_muse_sdk_id_t a, hb_muse_sdk_id_t b)
{
    return a.generation == b.generation && a.turn == b.turn;
}
static bool port_newer(hb_muse_sdk_id_t a, hb_muse_sdk_id_t b)
{
    return a.generation && a.turn && (a.generation > b.generation ||
           (a.generation == b.generation && a.turn > b.turn));
}
static bool port_active(void)
{
    portENTER_CRITICAL(&s_port_lock);
    bool active = port_session_allowed_locked() && s_port.id.generation && s_port.phase != HB_MUSE_SDK_IDLE &&
        s_port.phase != HB_MUSE_SDK_CANCEL_PENDING && s_port.phase != HB_MUSE_SDK_FAILED &&
        s_port.phase != HB_MUSE_SDK_DONE && s_port.phase != HB_MUSE_SDK_FENCED;
    portEXIT_CRITICAL(&s_port_lock);
    return active;
}
static void port_connected(bool subscribed)
{
    s_port_subscribed = subscribed;
    portENTER_CRITICAL(&s_port_lock);
    s_port.session_ready = subscribed && port_session_allowed_locked();
    portEXIT_CRITICAL(&s_port_lock);
}
static void port_worker_started(void)
{
    portENTER_CRITICAL(&s_port_lock);
    s_port.worker_started = true;
    portEXIT_CRITICAL(&s_port_lock);
}
static void port_note_ack(void)
{
    portENTER_CRITICAL(&s_port_lock);
    if (s_port.phase == HB_MUSE_SDK_WAIT_REPLY) s_port.note_ack = true;
    portEXIT_CRITICAL(&s_port_lock);
}
static void port_reply(const char *text, size_t bytes, unsigned index)
{
    if (!text || !text[0] || !bytes) return;
    portENTER_CRITICAL(&s_port_lock);
    if (s_port.phase == HB_MUSE_SDK_WAIT_REPLY && s_port.note_ack) {
        size_t n = strnlen(text, HB_MUSE_SDK_TEXT_CAP - 1u);
        memcpy(s_port.text, text, n);
        s_port.text[n] = '\0';
        /* Do not expose an incomplete UTF-8 suffix after truncation. */
        while (n && ((unsigned char)s_port.text[n - 1] & 0xc0u) == 0x80u) --n;
        if (n && (unsigned char)s_port.text[n - 1] >= 0xc0u) {
            unsigned char lead = (unsigned char)s_port.text[n - 1];
            size_t need = lead < 0xe0u ? 2u : lead < 0xf0u ? 3u : 4u;
            if (strlen(s_port.text + n - 1) < need) s_port.text[n - 1] = '\0';
        }
        s_port.text_truncated = bytes >= HB_MUSE_SDK_TEXT_CAP;
        s_port.message_index = index;
        if (s_port.text_revision == UINT32_MAX) s_port.phase = HB_MUSE_SDK_FAILED;
        else ++s_port.text_revision;
    }
    portEXIT_CRITICAL(&s_port_lock);
}
static void port_terminal(bool complete)
{
    /* Called by the worker AFTER turn_finish. No output producer remains;
     * headless turns are text-only and never generate fake speaker PCM. */
    drain_out();
    portENTER_CRITICAL(&s_port_lock);
    if (s_port.id.generation && (s_port.phase == HB_MUSE_SDK_BEGIN_PENDING ||
        s_port.phase == HB_MUSE_SDK_LISTENING || s_port.phase == HB_MUSE_SDK_END_PENDING ||
        s_port.phase == HB_MUSE_SDK_WAIT_REPLY)) {
        s_port.phase = complete && s_port.end_ack && s_port.note_ack &&
                       s_port.text_revision && s_port.text[0] ? HB_MUSE_SDK_DONE : HB_MUSE_SDK_FAILED;
    }
    portEXIT_CRITICAL(&s_port_lock);
}

static hb_muse_sdk_dispatch_t port_post(cmd_type_t type, hb_muse_sdk_id_t id)
{
    if (!s_cmds) return HB_MUSE_SDK_REJECTED;
    cmd_t cmd{};
    cmd.type = type;
    cmd.port_generation = id.generation;
    cmd.port_turn = id.turn;
    return xQueueSend(s_cmds, &cmd, 0) == pdTRUE ? HB_MUSE_SDK_QUEUED : HB_MUSE_SDK_BLOCKED;
}
extern "C" bool hb_muse_sdk_snapshot(hb_muse_sdk_snapshot_t *out)
{
    if (!out) return false;
    portENTER_CRITICAL(&s_port_lock);
    *out = s_port;
    portEXIT_CRITICAL(&s_port_lock);
    return true;
}
extern "C" hb_muse_sdk_dispatch_t hb_muse_sdk_begin(hb_muse_sdk_id_t id)
{
    portENTER_CRITICAL(&s_port_lock);
    if (!port_session_allowed_locked() || !port_newer(id, s_port_last) || !s_port.worker_started ||
        !s_port.session_ready || s_port.phase != HB_MUSE_SDK_IDLE) {
        portEXIT_CRITICAL(&s_port_lock);
        return HB_MUSE_SDK_REJECTED;
    }
    s_port.id = id;
    s_port.phase = HB_MUSE_SDK_BEGIN_PENDING;
    portEXIT_CRITICAL(&s_port_lock);
    hb_muse_sdk_dispatch_t result = port_post(CMD_PORT_BEGIN, id);
    portENTER_CRITICAL(&s_port_lock);
    if (result == HB_MUSE_SDK_QUEUED) s_port_last = id;
    else { s_port.id = {}; s_port.phase = HB_MUSE_SDK_IDLE; }
    portEXIT_CRITICAL(&s_port_lock);
    return result;
}
extern "C" hb_muse_sdk_audio_t hb_muse_sdk_audio(hb_muse_sdk_id_t id, uint32_t first,
                                                const int16_t *pcm, size_t frames)
{
    portENTER_CRITICAL(&s_port_lock);
    bool valid = port_equal(id, s_port.id) && s_port.phase == HB_MUSE_SDK_LISTENING &&
        s_port.begin_ack && s_port.session_ready && first == s_port.accepted_frames &&
        pcm && frames && frames <= 320u && first <= HB_MUSE_SDK_MAX_FRAMES &&
        frames <= HB_MUSE_SDK_MAX_FRAMES - first;
    portEXIT_CRITICAL(&s_port_lock);
    if (!valid || !s_in) return {0, true};
    /* Single producer. Worker resets only before BEGIN_ACK or after a CANCEL
     * submitted by this same owner, which has already revoked new PCM. */
    size_t bytes = xStreamBufferSend(s_in, pcm, frames * sizeof(*pcm), 0);
    if (bytes > frames * sizeof(*pcm) || bytes % sizeof(*pcm)) return {0, true}; /* unknown prefix: cancel */
    size_t taken = bytes / sizeof(*pcm);
    portENTER_CRITICAL(&s_port_lock);
    bool still_valid = port_equal(id, s_port.id) && s_port.phase == HB_MUSE_SDK_LISTENING;
    if (still_valid) s_port.accepted_frames += (uint32_t)taken;
    portEXIT_CRITICAL(&s_port_lock);
    return {taken, !still_valid}; /* never replay if a concurrent failure won */
}
extern "C" hb_muse_sdk_dispatch_t hb_muse_sdk_end(hb_muse_sdk_id_t id, uint32_t total)
{
    portENTER_CRITICAL(&s_port_lock);
    bool valid = port_equal(id, s_port.id) && s_port.phase == HB_MUSE_SDK_LISTENING &&
        total && total == s_port.accepted_frames;
    if (valid) s_port.phase = HB_MUSE_SDK_END_PENDING;
    portEXIT_CRITICAL(&s_port_lock);
    if (!valid) return HB_MUSE_SDK_REJECTED;
    hb_muse_sdk_dispatch_t result = port_post(CMD_PORT_END, id);
    if (result != HB_MUSE_SDK_QUEUED) {
        portENTER_CRITICAL(&s_port_lock);
        if (s_port.phase == HB_MUSE_SDK_END_PENDING) s_port.phase = HB_MUSE_SDK_LISTENING;
        portEXIT_CRITICAL(&s_port_lock);
    }
    return result;
}
extern "C" hb_muse_sdk_dispatch_t hb_muse_sdk_cancel(hb_muse_sdk_id_t id)
{
    portENTER_CRITICAL(&s_port_lock);
    bool valid = id.generation && port_equal(id, s_port.id) && s_port.phase != HB_MUSE_SDK_IDLE;
    if (!valid || s_port.phase == HB_MUSE_SDK_FENCED || s_port_cancel_queued) {
        portEXIT_CRITICAL(&s_port_lock);
        return HB_MUSE_SDK_REJECTED;
    }
    s_port.phase = HB_MUSE_SDK_CANCEL_PENDING;
    /* Set before enqueue: worker may consume immediately on another core. */
    s_port_cancel_queued = true;
    portEXIT_CRITICAL(&s_port_lock);
    hb_muse_sdk_dispatch_t result = port_post(CMD_PORT_CANCEL, id);
    if (result != HB_MUSE_SDK_QUEUED) {
        portENTER_CRITICAL(&s_port_lock);
        s_port_cancel_queued = false;
        portEXIT_CRITICAL(&s_port_lock);
    }
    return result;
}
extern "C" bool hb_muse_sdk_retire(hb_muse_sdk_id_t id)
{
    portENTER_CRITICAL(&s_port_lock);
    bool valid = port_equal(id, s_port.id) &&
        (s_port.phase == HB_MUSE_SDK_DONE || s_port.phase == HB_MUSE_SDK_FENCED);
    if (valid) {
        bool started = s_port.worker_started, ready = s_port.session_ready;
        s_port = {};
        s_port.worker_started = started;
        s_port.session_ready = ready;
        s_port_cancel_queued = false;
    }
    portEXIT_CRITICAL(&s_port_lock);
    return valid;
}

static void port_handle(const cmd_t &cmd)
{
    hb_muse_sdk_id_t id{cmd.port_generation, cmd.port_turn};
    portENTER_CRITICAL(&s_port_lock);
    bool matches = id.generation && port_equal(id, s_port.id);
    hb_muse_sdk_phase_t phase = s_port.phase;
    portEXIT_CRITICAL(&s_port_lock);
    if (!matches) return;
    if (cmd.type == CMD_PORT_CANCEL && phase == HB_MUSE_SDK_CANCEL_PENDING) {
        /* Local fence: serialized worker closes streams/connection, then
         * discards old queues. This cannot retract an already uploaded note. */
        turn_finish();
        disconnect("local turn cancelled");
        xStreamBufferReset(s_in);
        drain_out();
        xQueueReset(s_events);
        s_auto_next_us = 0;
        portENTER_CRITICAL(&s_port_lock);
        s_port.phase = HB_MUSE_SDK_FENCED;
        s_port.text[0] = '\0';
        portEXIT_CRITICAL(&s_port_lock);
    } else if (cmd.type == CMD_PORT_BEGIN && phase == HB_MUSE_SDK_BEGIN_PENDING) {
        if (!s_connected || !s_port_subscribed || s_turn.phase != P_IDLE || s_gen.load() == UINT32_MAX) {
            port_terminal(false);
            return;
        }
        xStreamBufferReset(s_in);
        drain_out();
        xQueueReset(s_events);
        turn_begin(++s_gen);
        portENTER_CRITICAL(&s_port_lock);
        if (s_port.phase == HB_MUSE_SDK_BEGIN_PENDING) {
            if (s_turn.phase == P_LISTEN) {
                s_port.begin_ack = true;
                s_port.phase = HB_MUSE_SDK_LISTENING;
            } else s_port.phase = HB_MUSE_SDK_FAILED;
        }
        portEXIT_CRITICAL(&s_port_lock);
    } else if (cmd.type == CMD_PORT_END && phase == HB_MUSE_SDK_END_PENDING) {
        if (s_turn.phase != P_LISTEN) { port_terminal(false); return; }
        s_turn.end_requested = true;
        portENTER_CRITICAL(&s_port_lock);
        if (s_port.phase == HB_MUSE_SDK_END_PENDING) {
            s_port.end_ack = true;
            s_port.phase = HB_MUSE_SDK_WAIT_REPLY;
        }
        portEXIT_CRITICAL(&s_port_lock);
    }
}
