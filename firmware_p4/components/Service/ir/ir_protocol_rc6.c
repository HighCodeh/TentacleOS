// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ir_protocol.h"
#include "ir_protocol_rc6.h"

static const char *TAG = "IR_RC6";

// ══════════════════════════════════════════════════════
// Biphase decoder
// ══════════════════════════════════════════════════════

typedef struct {
    uint32_t *buf;
    size_t    len;
    size_t    pos;
    uint32_t  units;
    uint32_t  unit;
} biphase_t;

static void bp_init(biphase_t *bp, uint32_t *buf, size_t len, uint32_t unit) {
    bp->buf = buf; bp->len = len; bp->pos = 0; bp->unit = unit;
    bp->units = (len > 0) ? (buf[0] + unit / 2) / unit : 0;
}

static int bp_get(biphase_t *bp) {
    while (bp->pos < bp->len && bp->units == 0) {
        bp->pos++;
        if (bp->pos < bp->len)
            bp->units = (bp->buf[bp->pos] + bp->unit / 2) / bp->unit;
    }
    if (bp->pos >= bp->len) return -1;
    bp->units--;
    return (bp->pos % 2 == 0) ? 1 : 0;  // even=mark, odd=space
}

// ══════════════════════════════════════════════════════
// RC6 Decode
// ══════════════════════════════════════════════════════

bool rc6_decode(rmt_symbol_word_t *symbols, size_t count, ir_data_t *out) {
    if (!ir_match(symbols[0].duration0, RC6_HEADER_MARK) ||
        !ir_match(symbols[0].duration1, RC6_HEADER_SPACE))
        return false;

    // Build flat duration array from symbols after header
    uint32_t flat[128];
    size_t flat_len = 0;
    for (size_t i = 1; i < count && flat_len < 126; i++) {
        if (symbols[i].duration0) flat[flat_len++] = symbols[i].duration0;
        if (symbols[i].duration1) flat[flat_len++] = symbols[i].duration1;
    }

    biphase_t bp;
    bp_init(&bp, flat, flat_len, RC6_UNIT);

    uint32_t raw = 0;
    int decoded = 0;

    for (int i = 0; i < RC6_TOTAL_BITS; i++) {
        int half = (i == RC6_TOGGLE_INDEX) ? 2 : 1;

        int first = -1;
        for (int u = 0; u < half; u++) {
            int l = bp_get(&bp);
            if (l < 0) goto done;
            if (u == 0) first = l;
        }

        int second = -1;
        for (int u = 0; u < half; u++) {
            int l = bp_get(&bp);
            if (l < 0) goto done;
            if (u == 0) second = l;
        }

        // RC6 Manchester: mark->space = 1, space->mark = 0
        if (first == 1 && second == 0)
            raw |= (1UL << (RC6_TOTAL_BITS - 1 - i));
        else if (!(first == 0 && second == 1))
            return false;

        decoded++;
    }

done:
    if (decoded < 17) return false;

    out->protocol = IR_PROTO_RC6;
    out->address  = (raw >> 8) & 0xFF;
    out->command  = raw & 0xFF;
    out->repeat   = false;
    return true;
}

// ══════════════════════════════════════════════════════
// Manchester encoder helper
// ══════════════════════════════════════════════════════

typedef struct {
    rmt_symbol_word_t *buf;
    size_t count;
    uint32_t mark_us;
    uint32_t space_us;
} menc_t;

static void me_init(menc_t *me, rmt_symbol_word_t *buf) {
    me->buf = buf; me->count = 0; me->mark_us = 0; me->space_us = 0;
}

static void me_mark(menc_t *me, uint32_t us) {
    if (me->space_us > 0) {
        me->buf[me->count].duration0 = me->mark_us;  me->buf[me->count].level0 = 1;
        me->buf[me->count].duration1 = me->space_us;  me->buf[me->count].level1 = 0;
        me->count++;
        me->mark_us = us;
        me->space_us = 0;
    } else {
        me->mark_us += us;
    }
}

static void me_space(menc_t *me, uint32_t us) {
    me->space_us += us;
}

static size_t me_finish(menc_t *me) {
    if (me->mark_us > 0 || me->space_us > 0) {
        me->buf[me->count].duration0 = me->mark_us;  me->buf[me->count].level0 = 1;
        me->buf[me->count].duration1 = me->space_us;  me->buf[me->count].level1 = 0;
        me->count++;
    }
    return me->count;
}

// ══════════════════════════════════════════════════════
// RC6 Encode
// ══════════════════════════════════════════════════════

size_t rc6_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max) {
    static bool s_toggle = false;

    // 21 bits MSB: start(1) + mode(000) + toggle + addr(8) + cmd(8)
    uint32_t raw = (1UL << 20) |
                   (s_toggle ? (1UL << 16) : 0) |
                   ((data->address & 0xFF) << 8) |
                   (data->command & 0xFF);
    s_toggle = !s_toggle;

    // Header
    symbols[0].duration0 = RC6_HEADER_MARK;  symbols[0].level0 = 1;
    symbols[0].duration1 = RC6_HEADER_SPACE;  symbols[0].level1 = 0;

    // Manchester encode data bits
    menc_t me;
    me_init(&me, &symbols[1]);

    for (int i = 0; i < RC6_TOTAL_BITS; i++) {
        int bit = (raw >> (RC6_TOTAL_BITS - 1 - i)) & 1;
        uint32_t t = (i == RC6_TOGGLE_INDEX) ? RC6_UNIT * 2 : RC6_UNIT;

        // RC6: bit 1 = mark->space, bit 0 = space->mark
        if (bit) {
            me_mark(&me, t);
            me_space(&me, t);
        } else {
            me_space(&me, t);
            me_mark(&me, t);
        }
    }

    return 1 + me_finish(&me);
}
