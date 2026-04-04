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
#include "ir_protocol_rc5.h"

static const char *TAG = "IR_RC5";

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
    return (bp->pos % 2 == 0) ? 1 : 0;
}

// ══════════════════════════════════════════════════════
// RC5 Decode
// ══════════════════════════════════════════════════════

bool rc5_decode(rmt_symbol_word_t *symbols, size_t count, ir_data_t *out) {
    // RC5: no header. First mark must be ~889us or ~1778us (1 or 2 units)
    if (!ir_match(symbols[0].duration0, RC5_UNIT) &&
        !ir_match(symbols[0].duration0, RC5_UNIT * 2))
        return false;

    // Build flat array
    uint32_t flat[64];
    size_t flat_len = 0;
    for (size_t i = 0; i < count && flat_len < 62; i++) {
        if (symbols[i].duration0) flat[flat_len++] = symbols[i].duration0;
        if (symbols[i].duration1) flat[flat_len++] = symbols[i].duration1;
    }

    biphase_t bp;
    bp_init(&bp, flat, flat_len, RC5_UNIT);

    // Start bit: first captured level must be mark (2nd half of start bit = 1)
    if (bp_get(&bp) != 1) return false;

    // Decode 13 bits: field(1) + toggle(1) + addr(5) + cmd(6)
    uint32_t raw = 0;
    int decoded = 0;

    for (int i = 0; i < 13; i++) {
        int a = bp_get(&bp);
        int b = bp_get(&bp);
        if (a < 0 || b < 0) break;

        // RC5 biphase: space->mark = 1, mark->space = 0
        if (a == 0 && b == 1)
            raw |= (1UL << (12 - i));  // MSB first
        else if (!(a == 1 && b == 0))
            break;

        decoded++;
    }

    if (decoded < 11) return false;

    out->protocol = IR_PROTO_RC5;
    out->command  = raw & 0x3F;
    out->address  = (raw >> 6) & 0x1F;
    out->repeat   = false;

    // Field bit = 0 -> RC5X: 7th command bit
    if (!((raw >> 12) & 1)) {
        out->command |= 0x40;
    }

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
// RC5 Encode
// ══════════════════════════════════════════════════════

size_t rc5_encode(const ir_data_t *data, rmt_symbol_word_t *symbols, size_t max) {
    static bool s_toggle = false;

    uint8_t cmd = data->command;
    bool field = (cmd <= 0x3F);  // field=1 for standard RC5, 0 for RC5X

    // 14 bits MSB first: start(1) + field + toggle + addr(5) + cmd(6)
    uint16_t raw = (1U << 13) |
                   (field ? (1U << 12) : 0) |
                   (s_toggle ? (1U << 11) : 0) |
                   ((data->address & 0x1F) << 6) |
                   (cmd & 0x3F);
    s_toggle = !s_toggle;

    menc_t me;
    me_init(&me, symbols);

    for (int i = 0; i < 14; i++) {
        int bit = (raw >> (13 - i)) & 1;

        // RC5: bit 0 = mark->space, bit 1 = space->mark
        if (bit == 0) {
            me_mark(&me, RC5_UNIT);
            me_space(&me, RC5_UNIT);
        } else {
            if (me.mark_us == 0 && me.space_us == 0 && me.count == 0) {
                // First bit is start=1: skip initial space (already the pre-transmission gap)
                me_mark(&me, RC5_UNIT);
            } else {
                me_space(&me, RC5_UNIT);
                me_mark(&me, RC5_UNIT);
            }
        }
    }

    return me_finish(&me);
}
