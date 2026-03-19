/**
 * @file iso15693_emu.c
 * @brief ISO 15693 (NFC-V) tag emulation for ST25R3916.
 *
 * Architecture is a direct port of t2t_emu.c with the following differences:
 *
 *  1. MODE register: 0xB0 instead of 0x88  (0x80=target | 0x30=NFC-V)
 *
 *  2. No hardware anti-collision for NFC-V.
 *     In NFC-A the chip replies to REQA/ATQA automatically via PT Memory.
 *     In NFC-V there is no equivalent: the chip receives the full INVENTORY
 *     frame and fires RXE; we must build and transmit the response ourselves.
 *
 *  3. Command framing:
 *     Every request: [flags][cmd][uid if addressed][params][CRC]
 *     Every response: [resp_flags][data][CRC]  (no parity bits)
 *
 *  4. CRC-16 (poly=0x8408, init=0xFFFF, ~result) appended to every TX.
 *     CMD_TX_WITH_CRC in NFC-V mode handles this automatically.
 *
 *  5. SLEEP/SENSE/ACTIVE state machine is identical to T2T.
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * CRITICAL REGISTER DIFFERENCES vs NFC-A target:
 *
 *  REG_MODE         = 0xB0  (target=1, om=0110 = ISO 15693)
 *  REG_BIT_RATE     = 0x00  (26.48 kbps)
 *  REG_ISO14443B_FELICA = 0x00 (single subcarrier)
 *  REG_PASSIVE_TARGET: not used for NFC-V
 *  REG_AUX_DEF     : not used for NFC-V (no UID size config needed)
 *  PT Memory       : not applicable (NFC-V has no hardware anticol memory)
 *
 * ─────────────────────────────────────────────────────────────────────────────
 * Field-detector wake-up (same as T2T):
 *
 *  OP_CTRL = 0xC3 (EN + RX_EN + en_fd_c=11)
 *  CMD_GOTO_SLEEP → monitor efd / IRQ_TGT_WU_A
 *  CMD_GOTO_SENSE → wait for RXE (INVENTORY or any command)
 */
#include "iso15693_emu.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"
#include "st25r3916_fifo.h"
#include "st25r3916_irq.h"
#include "hb_nfc_spi.h"

static const char* TAG = "iso15693_emu";

/* ─── Constants ─────────────────────────────────────────────────────────────── */
#define MODE_TARGET_NFCV    0xB0U   /**< target=1, om=NFC-V               */
#define OP_CTRL_TARGET      0xC3U   /**< EN + RX_EN + en_fd_c=11           */
#define TIMER_I_EON          0x10U   /**< external field detected */
#define ACTIVE_IDLE_TIMEOUT  2000U  /**< ~4s if run_step every 2ms */
#define RESP_FLAGS_OK       0x00U   /**< no error                           */
#define RESP_FLAGS_ERR      0x01U   /**< error flag set                     */

/* ─── State machine ─────────────────────────────────────────────────────────── */
typedef enum {
    ISO15693_STATE_SLEEP,    /**< low power, field detector on              */
    ISO15693_STATE_SENSE,    /**< ready to receive first command             */
    ISO15693_STATE_ACTIVE,   /**< tag selected / responding                 */
} iso15693_emu_state_t;

/* ─── Module state ──────────────────────────────────────────────────────────── */
static iso15693_emu_card_t  s_card;
static iso15693_emu_state_t s_state     = ISO15693_STATE_SLEEP;
static bool                 s_quiet     = false;  /**< STAY_QUIET received  */
static bool                 s_init_done = false;
static uint32_t             s_sense_idle = 0;     /**< iterations in SENSE without RX */
static uint32_t             s_active_idle = 0;    /**< iterations in ACTIVE without RX */
static bool                 s_block_locked[ISO15693_EMU_MAX_BLOCKS];
static bool                 s_afi_locked = false;
static bool                 s_dsfid_locked = false;

/* ─── Helpers ────────────────────────────────────────────────────────────────── */

static bool wait_oscillator(int timeout_ms)
{
    for (int i = 0; i < timeout_ms; i++) {
        uint8_t aux = 0, mi = 0;
        hb_spi_reg_read(REG_AUX_DISPLAY, &aux);
        hb_spi_reg_read(REG_MAIN_INT,    &mi);
        if ((aux & 0x04U) || (mi & 0x80U)) {
            ESP_LOGI(TAG, "Osc OK in %dms", i);
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_LOGW(TAG, "Osc timeout — continuing");
    return false;
}

/** Append CRC-16 and transmit with CMD_TX_WITH_CRC.
 *  The chip appends CRC automatically in NFC-V mode. */
static void tx_response(const uint8_t* data, int len)
{
    if (!data || len <= 0) return;
    st25r_fifo_clear();
    st25r_set_tx_bytes((uint16_t)len, 0);
    st25r_fifo_load(data, (size_t)len);
    hb_spi_direct_cmd(CMD_TX_WITH_CRC);
}

static void tx_error(uint8_t err_code)
{
    uint8_t resp[2] = { RESP_FLAGS_ERR, err_code };
    tx_response(resp, 2);
}

/** Read FIFO into buf, return byte count. */
static int fifo_rx(uint8_t* buf, size_t max)
{
    uint8_t fs1 = 0;
    hb_spi_reg_read(REG_FIFO_STATUS1, &fs1);
    int n = (int)(fs1 & 0x7FU);
    if (n <= 0) return 0;
    if ((size_t)n > max) n = (int)max;
    st25r_fifo_read(buf, (size_t)n);
    return n;
}

/** Return true if the request is addressed to us (UID matches). */
static bool uid_matches(const uint8_t* frame, int len, int uid_offset)
{
    if (len < uid_offset + (int)ISO15693_UID_LEN) return false;
    return (memcmp(&frame[uid_offset], s_card.uid, ISO15693_UID_LEN) == 0);
}

/** Return true if command targets us: either broadcast or our UID. */
static bool is_for_us(const uint8_t* frame, int len)
{
    if (len < 2) return false;
    uint8_t flags = frame[0];
    if (flags & ISO15693_FLAG_SELECT) {
        /* SELECT flag: only respond if we are in selected state */
        return (s_state == ISO15693_STATE_ACTIVE);
    }
    if (flags & ISO15693_FLAG_ADDRESS) {
        /* ADDRESSED mode: UID follows cmd byte (offset 2) */
        return uid_matches(frame, len, 2);
    }
    /* BROADCAST mode: always for us */
    return true;
}

/* ─── Command handlers ───────────────────────────────────────────────────────── */

static void handle_inventory(const uint8_t* frame, int len)
{
    (void)len;
    if (s_quiet) return;   /* STAY_QUIET: do not respond to INVENTORY */

    /*
     * Response: [RESP_FLAGS_OK][DSFID][UID×8]
     * No CRC appended here — CMD_TX_WITH_CRC handles it.
     */
    uint8_t resp[10];
    resp[0] = RESP_FLAGS_OK;
    resp[1] = s_card.dsfid;
    memcpy(&resp[2], s_card.uid, ISO15693_UID_LEN);

    ESP_LOGI(TAG, "INVENTORY → replying with UID");
    tx_response(resp, sizeof(resp));

    s_state = ISO15693_STATE_ACTIVE;
    s_quiet = false;
}

static void handle_get_system_info(const uint8_t* frame, int len)
{
    if (!is_for_us(frame, len)) return;

    /*
     * Response:
     *   [RESP_FLAGS_OK][info_flags]
     *   [UID×8]
     *   [DSFID]         ← if info_flags bit0
     *   [AFI]           ← if info_flags bit1
     *   [blk_cnt-1][blk_sz-1]  ← if info_flags bit2
     *   [IC_ref]        ← if info_flags bit3
     */
    uint8_t resp[32];
    int pos = 0;

    resp[pos++] = RESP_FLAGS_OK;
    resp[pos++] = 0x0FU;   /* all 4 info fields present */
    memcpy(&resp[pos], s_card.uid, ISO15693_UID_LEN);
    pos += ISO15693_UID_LEN;
    resp[pos++] = s_card.dsfid;
    resp[pos++] = s_card.afi;
    resp[pos++] = (uint8_t)(s_card.block_count - 1U);   /* n-1 encoding */
    resp[pos++] = (uint8_t)((s_card.block_size  - 1U) & 0x1FU);
    resp[pos++] = s_card.ic_ref;

    ESP_LOGI(TAG, "GET_SYSTEM_INFO → blocks=%u size=%u",
             s_card.block_count, s_card.block_size);
    tx_response(resp, pos);
}

static void handle_read_single_block(const uint8_t* frame, int len)
{
    if (!is_for_us(frame, len)) return;

    /* block number is after [flags][cmd][uid×8] = byte 10 */
    int blk_offset = (frame[0] & ISO15693_FLAG_ADDRESS) ? 10 : 2;
    if (len < blk_offset + 1) { tx_error(ISO15693_ERR_NOT_RECOGNIZED); return; }

    uint8_t block = frame[blk_offset];
    if (block >= s_card.block_count) {
        tx_error(ISO15693_ERR_BLOCK_UNAVAILABLE);
        return;
    }

    const uint8_t* bdata = &s_card.mem[block * s_card.block_size];

    uint8_t resp[1 + ISO15693_MAX_BLOCK_SIZE];
    resp[0] = RESP_FLAGS_OK;
    memcpy(&resp[1], bdata, s_card.block_size);

    ESP_LOGI(TAG, "READ_SINGLE_BLOCK[%u]", block);
    tx_response(resp, 1 + (int)s_card.block_size);
}

static void handle_write_single_block(const uint8_t* frame, int len)
{
    if (!is_for_us(frame, len)) return;

    int blk_offset  = (frame[0] & ISO15693_FLAG_ADDRESS) ? 10 : 2;
    int data_offset = blk_offset + 1;

    if (len < data_offset + (int)s_card.block_size) {
        tx_error(ISO15693_ERR_NOT_RECOGNIZED);
        return;
    }

    uint8_t block = frame[blk_offset];
    if (block >= s_card.block_count) {
        tx_error(ISO15693_ERR_BLOCK_UNAVAILABLE);
        return;
    }
    if (s_block_locked[block]) {
        tx_error(ISO15693_ERR_BLOCK_LOCKED);
        return;
    }

    memcpy(&s_card.mem[block * s_card.block_size],
           &frame[data_offset], s_card.block_size);

    uint8_t resp[1] = { RESP_FLAGS_OK };
    ESP_LOGI(TAG, "WRITE_SINGLE_BLOCK[%u]", block);
    tx_response(resp, 1);
}

static void handle_read_multiple_blocks(const uint8_t* frame, int len)
{
    if (!is_for_us(frame, len)) return;

    int blk_offset = (frame[0] & ISO15693_FLAG_ADDRESS) ? 10 : 2;
    if (len < blk_offset + 2) { tx_error(ISO15693_ERR_NOT_RECOGNIZED); return; }

    uint8_t first = frame[blk_offset];
    uint8_t count = (uint8_t)(frame[blk_offset + 1] + 1U);  /* n-1 → n */

    if ((unsigned)(first + count) > s_card.block_count) {
        tx_error(ISO15693_ERR_BLOCK_UNAVAILABLE);
        return;
    }

    static uint8_t resp[1 + ISO15693_EMU_MEM_SIZE];
    resp[0] = RESP_FLAGS_OK;
    size_t total = (size_t)count * s_card.block_size;
    memcpy(&resp[1], &s_card.mem[first * s_card.block_size], total);

    ESP_LOGI(TAG, "READ_MULTIPLE_BLOCKS[%u+%u]", first, count);
    tx_response(resp, (int)(1U + total));
}

static void handle_lock_block(const uint8_t* frame, int len)
{
    if (!is_for_us(frame, len)) return;

    int blk_offset = (frame[0] & ISO15693_FLAG_ADDRESS) ? 10 : 2;
    if (len < blk_offset + 1) { tx_error(ISO15693_ERR_NOT_RECOGNIZED); return; }

    uint8_t block = frame[blk_offset];
    if (block >= s_card.block_count) {
        tx_error(ISO15693_ERR_BLOCK_UNAVAILABLE);
        return;
    }

    s_block_locked[block] = true;
    uint8_t resp[1] = { RESP_FLAGS_OK };
    ESP_LOGI(TAG, "LOCK_BLOCK[%u]", block);
    tx_response(resp, 1);
}

static void handle_write_afi(const uint8_t* frame, int len)
{
    if (!is_for_us(frame, len)) return;
    int off = (frame[0] & ISO15693_FLAG_ADDRESS) ? 10 : 2;
    if (len < off + 1) { tx_error(ISO15693_ERR_NOT_RECOGNIZED); return; }
    if (s_afi_locked) { tx_error(ISO15693_ERR_LOCK_FAILED); return; }
    s_card.afi = frame[off];
    uint8_t resp[1] = { RESP_FLAGS_OK };
    ESP_LOGI(TAG, "WRITE_AFI=0x%02X", s_card.afi);
    tx_response(resp, 1);
}

static void handle_lock_afi(const uint8_t* frame, int len)
{
    if (!is_for_us(frame, len)) return;
    s_afi_locked = true;
    uint8_t resp[1] = { RESP_FLAGS_OK };
    ESP_LOGI(TAG, "LOCK_AFI");
    tx_response(resp, 1);
}

static void handle_write_dsfid(const uint8_t* frame, int len)
{
    if (!is_for_us(frame, len)) return;
    int off = (frame[0] & ISO15693_FLAG_ADDRESS) ? 10 : 2;
    if (len < off + 1) { tx_error(ISO15693_ERR_NOT_RECOGNIZED); return; }
    if (s_dsfid_locked) { tx_error(ISO15693_ERR_LOCK_FAILED); return; }
    s_card.dsfid = frame[off];
    uint8_t resp[1] = { RESP_FLAGS_OK };
    ESP_LOGI(TAG, "WRITE_DSFID=0x%02X", s_card.dsfid);
    tx_response(resp, 1);
}

static void handle_lock_dsfid(const uint8_t* frame, int len)
{
    if (!is_for_us(frame, len)) return;
    s_dsfid_locked = true;
    uint8_t resp[1] = { RESP_FLAGS_OK };
    ESP_LOGI(TAG, "LOCK_DSFID");
    tx_response(resp, 1);
}

static void handle_get_multi_block_sec(const uint8_t* frame, int len)
{
    if (!is_for_us(frame, len)) return;

    int blk_offset = (frame[0] & ISO15693_FLAG_ADDRESS) ? 10 : 2;
    if (len < blk_offset + 2) { tx_error(ISO15693_ERR_NOT_RECOGNIZED); return; }

    uint8_t first = frame[blk_offset];
    uint8_t count = (uint8_t)(frame[blk_offset + 1] + 1U);
    if ((unsigned)(first + count) > s_card.block_count) {
        tx_error(ISO15693_ERR_BLOCK_UNAVAILABLE);
        return;
    }

    uint8_t resp[1 + ISO15693_EMU_MAX_BLOCKS];
    resp[0] = RESP_FLAGS_OK;
    for (uint8_t i = 0; i < count; i++) {
        uint8_t block = (uint8_t)(first + i);
        resp[1 + i] = s_block_locked[block] ? 0x01U : 0x00U;
    }
    ESP_LOGI(TAG, "GET_MULTI_BLOCK_SEC[%u+%u]", first, count);
    tx_response(resp, 1 + count);
}

static void handle_stay_quiet(const uint8_t* frame, int len)
{
    if (!uid_matches(frame, len, 2)) return;
    ESP_LOGI(TAG, "STAY_QUIET → silent until power cycle");
    s_quiet = true;
    s_state = ISO15693_STATE_SLEEP;
    hb_spi_direct_cmd(CMD_GOTO_SLEEP);
}

/* ─── Public: card setup ──────────────────────────────────────────────────────  */

void iso15693_emu_card_from_tag(iso15693_emu_card_t*   card,
                                 const iso15693_tag_t*  tag,
                                 const uint8_t*         raw_mem)
{
    if (!card || !tag) return;
    memset(card, 0, sizeof(*card));
    memcpy(card->uid, tag->uid, ISO15693_UID_LEN);
    card->dsfid       = tag->dsfid;
    card->afi         = tag->afi;
    card->ic_ref      = tag->ic_ref;
    card->block_count = tag->block_count;
    card->block_size  = tag->block_size;
    if (raw_mem) {
        size_t mem_bytes = (size_t)tag->block_count * tag->block_size;
        if (mem_bytes > ISO15693_EMU_MEM_SIZE) mem_bytes = ISO15693_EMU_MEM_SIZE;
        memcpy(card->mem, raw_mem, mem_bytes);
    }
}

void iso15693_emu_card_default(iso15693_emu_card_t* card,
                                const uint8_t uid[ISO15693_UID_LEN])
{
    if (!card) return;
    memset(card, 0, sizeof(*card));
    memcpy(card->uid, uid, ISO15693_UID_LEN);
    card->dsfid       = 0x00U;
    card->afi         = 0x00U;
    card->ic_ref      = 0x01U;
    card->block_count = 8;
    card->block_size  = 4;
    /* Leave mem zeroed */
    ESP_LOGI(TAG, "Default card: 8 blocks × 4 bytes");
}

hb_nfc_err_t iso15693_emu_init(const iso15693_emu_card_t* card)
{
    if (!card) return HB_NFC_ERR_PARAM;
    if (card->block_count > ISO15693_EMU_MAX_BLOCKS ||
        card->block_size  > ISO15693_EMU_MAX_BLOCK_SIZE ||
        card->block_count == 0 || card->block_size == 0) {
        ESP_LOGE(TAG, "Invalid card params: blocks=%u size=%u",
                 card->block_count, card->block_size);
        return HB_NFC_ERR_PARAM;
    }

    memcpy(&s_card, card, sizeof(s_card));
    s_state    = ISO15693_STATE_SLEEP;
    s_quiet    = false;
    s_init_done = true;
    memset(s_block_locked, 0, sizeof(s_block_locked));
    s_afi_locked = false;
    s_dsfid_locked = false;

    char uid_str[20];
    snprintf(uid_str, sizeof(uid_str),
             "%02X%02X%02X%02X%02X%02X%02X%02X",
             card->uid[7], card->uid[6], card->uid[5], card->uid[4],
             card->uid[3], card->uid[2], card->uid[1], card->uid[0]);
    ESP_LOGI(TAG, "Init: UID=%s  blocks=%u  block_size=%u",
             uid_str, card->block_count, card->block_size);
    return HB_NFC_OK;
}

/* ─── Public: target configure ────────────────────────────────────────────────  */

hb_nfc_err_t iso15693_emu_configure_target(void)
{
    if (!s_init_done) return HB_NFC_ERR_INTERNAL;

    ESP_LOGI(TAG, "Configure NFC-V target");

    /* 1. Reset chip */
    hb_spi_reg_write(REG_OP_CTRL, 0x00U);
    vTaskDelay(pdMS_TO_TICKS(5));
    hb_spi_direct_cmd(CMD_SET_DEFAULT);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Verify chip alive */
    uint8_t ic = 0;
    hb_spi_reg_read(REG_IC_IDENTITY, &ic);
    if (ic == 0x00U || ic == 0xFFU) {
        ESP_LOGE(TAG, "Chip not responding: IC=0x%02X", ic);
        return HB_NFC_ERR_INTERNAL;
    }

    /* 2. Oscillator */
    hb_spi_reg_write(REG_IO_CONF2, 0x80U);
    vTaskDelay(pdMS_TO_TICKS(2));
    hb_spi_reg_write(REG_OP_CTRL, 0x80U);
    wait_oscillator(200);

    /* 3. Regulators */
    hb_spi_direct_cmd(CMD_ADJUST_REGULATORS);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* 4. Core registers
     *
     *  REG_MODE = 0xB0: target=1, om=0110 (ISO 15693)
     *  REG_BIT_RATE = 0x00: 26.48 kbps
     *  REG_ISO14443B_FELICA = 0x00: single subcarrier, default
     *  REG_PASSIVE_TARGET = 0x00: not used for NFC-V
     */
    hb_spi_reg_write(REG_MODE,             MODE_TARGET_NFCV);
    hb_spi_reg_write(REG_BIT_RATE,         0x00U);
    hb_spi_reg_write(REG_ISO14443B_FELICA, 0x00U);
    hb_spi_reg_write(REG_PASSIVE_TARGET,   0x00U);

    /* 5. Field thresholds (same values that work for T2T) */
    hb_spi_reg_write(REG_FIELD_THRESH_ACT,   0x00U);
    hb_spi_reg_write(REG_FIELD_THRESH_DEACT, 0x00U);

    /* 6. Modulation (resistive AM, same as T2T) */
    hb_spi_reg_write(REG_PT_MOD, 0x60U);

    /* 7. Unmask all IRQs */
    st25r_irq_read();
    hb_spi_reg_write(REG_MASK_MAIN_INT,      0x00U);
    hb_spi_reg_write(REG_MASK_TIMER_NFC_INT, 0x00U);
    hb_spi_reg_write(REG_MASK_ERROR_WUP_INT, 0x00U);
    hb_spi_reg_write(REG_MASK_TARGET_INT,    0x00U);

    /* 8. Verify */
    uint8_t mode_rb = 0;
    hb_spi_reg_read(REG_MODE, &mode_rb);
    if (mode_rb != MODE_TARGET_NFCV) {
        ESP_LOGE(TAG, "MODE readback: 0x%02X (expected 0x%02X)", mode_rb, MODE_TARGET_NFCV);
        return HB_NFC_ERR_INTERNAL;
    }

    ESP_LOGI(TAG, "NFC-V target configured (MODE=0x%02X)", mode_rb);
    return HB_NFC_OK;
}

/* ─── Public: start / stop ─────────────────────────────────────────────────────  */

hb_nfc_err_t iso15693_emu_start(void)
{
    if (!s_init_done) return HB_NFC_ERR_INTERNAL;

    st25r_irq_read();
    s_quiet = false;

    hb_spi_reg_write(REG_OP_CTRL, OP_CTRL_TARGET);
    vTaskDelay(pdMS_TO_TICKS(5));

    /* Enter SLEEP: field detector (en_fd_c=11) wakes us on external field */
    hb_spi_direct_cmd(CMD_GOTO_SLEEP);
    vTaskDelay(pdMS_TO_TICKS(2));

    s_state = ISO15693_STATE_SLEEP;

    uint8_t op = 0, aux = 0, pts = 0;
    hb_spi_reg_read(REG_OP_CTRL,            &op);
    hb_spi_reg_read(REG_AUX_DISPLAY,        &aux);
    hb_spi_reg_read(REG_PASSIVE_TARGET_STS, &pts);
    ESP_LOGI(TAG, "Start (SLEEP): OP=0x%02X AUX=0x%02X PT_STS=0x%02X",
             op, aux, pts);
    ESP_LOGI(TAG, "ISO 15693 emulation active — present the reader!");
    return HB_NFC_OK;
}

void iso15693_emu_stop(void)
{
    hb_spi_reg_write(REG_OP_CTRL, 0x00U);
    s_state    = ISO15693_STATE_SLEEP;
    s_init_done = false;
    ESP_LOGI(TAG, "ISO 15693 emulation stopped");
}

uint8_t* iso15693_emu_get_mem(void) { return s_card.mem; }

/* ─── Public: run step (state machine) ────────────────────────────────────────  */

void iso15693_emu_run_step(void)
{
    uint8_t tgt_irq   = 0;
    uint8_t main_irq  = 0;
    uint8_t timer_irq = 0;

    hb_spi_reg_read(REG_TARGET_INT,    &tgt_irq);
    hb_spi_reg_read(REG_MAIN_INT,      &main_irq);
    hb_spi_reg_read(REG_TIMER_NFC_INT, &timer_irq);

    uint8_t pts = 0;
    hb_spi_reg_read(REG_PASSIVE_TARGET_STS, &pts);
    uint8_t pta = pts & 0x0FU;

    /* ── SLEEP ──────────────────────────────────────────────────────────────── */
    if (s_state == ISO15693_STATE_SLEEP) {
        /*
         * Wake-up conditions:
         *  - I_eon (external field detected)
         *  - WU_A IRQ (legacy wake from field detector)
         *  - WU_F IRQ (can fire on some ST25R3916 revisions)
         */
        bool wake = (timer_irq & TIMER_I_EON)
                 || (tgt_irq & IRQ_TGT_WU_A)
                 || (tgt_irq & IRQ_TGT_WU_F);
        if (wake) {
            s_sense_idle = 0;
            s_active_idle = 0;
            ESP_LOGI(TAG, "SLEEP → SENSE (pta=%u tgt=0x%02X)", pta, tgt_irq);
            s_state = ISO15693_STATE_SENSE;
            hb_spi_direct_cmd(CMD_GOTO_SENSE);
            /* Re-read IRQs after state change */
            hb_spi_reg_read(REG_TARGET_INT,    &tgt_irq);
            hb_spi_reg_read(REG_MAIN_INT,      &main_irq);
            hb_spi_reg_read(REG_TIMER_NFC_INT, &timer_irq);
            if (!(main_irq & IRQ_MAIN_RXE)) return;
        } else {
            return;
        }
    }

    /* ── SENSE ──────────────────────────────────────────────────────────────── */
    if (s_state == ISO15693_STATE_SENSE) {
        /*
         * DO NOT check timer_irq & 0x08 (i_eof) here.
         *
         * ISO 15693 uses 100% ASK modulation: the reader briefly drops the
         * field to zero for every bit. The chip fires i_eof on each of those
         * pulses, which looks identical to a real field-loss event.  Checking
         * it in SENSE causes an immediate SLEEP before receiving INVENTORY.
         *
         * Stay in SENSE until:
         *  a) RXE fires  → INVENTORY received, go ACTIVE
         *  b) s_sense_idle > 250 (~500 ms)  → real field loss, back to SLEEP
         */
        if (main_irq & IRQ_MAIN_RXE) {
            s_sense_idle = 0;
            s_state = ISO15693_STATE_ACTIVE;
            /* Fall through to ACTIVE handling below */
        } else {
            if (++s_sense_idle > 250U) {
                ESP_LOGI(TAG, "SENSE: timeout (no RX in 500ms) → SLEEP");
                s_sense_idle = 0;
                s_state = ISO15693_STATE_SLEEP;
                hb_spi_direct_cmd(CMD_GOTO_SLEEP);
            }
            return;
        }
    }

    /* ── ACTIVE ──────────────────────────────────────────────────────────────── */
    if (!(main_irq & IRQ_MAIN_RXE)) {
        if (++s_active_idle >= ACTIVE_IDLE_TIMEOUT) {
            ESP_LOGI(TAG, "ACTIVE: idle timeout -> SLEEP");
            s_active_idle = 0;
            s_state = ISO15693_STATE_SLEEP;
            hb_spi_direct_cmd(CMD_GOTO_SLEEP);
            return;
        }
        if ((s_active_idle % 500U) == 0U) {
            uint8_t aux = 0;
            hb_spi_reg_read(REG_AUX_DISPLAY, &aux);
            ESP_LOGD(TAG, "[ACTIVE] AUX=0x%02X pta=%u", aux, pta);
        }
        return;
    }
    s_active_idle = 0;

    /* ── Read FIFO ──────────────────────────────────────────────────────────── */
    uint8_t buf[64] = { 0 };
    int len = fifo_rx(buf, sizeof(buf));
    if (len < 2) return;

    ESP_LOGI(TAG, "CMD: 0x%02X flags=0x%02X len=%d", buf[1], buf[0], len);

    /*
     * Frame: [flags][cmd_code][...]
     * buf[0] = request flags
     * buf[1] = command code
     */
    uint8_t cmd_code = buf[1];

    switch (cmd_code) {
    case ISO15693_CMD_INVENTORY:
        handle_inventory(buf, len);
        break;

    case ISO15693_CMD_STAY_QUIET:
        handle_stay_quiet(buf, len);
        break;

    case ISO15693_CMD_GET_SYSTEM_INFO:
        handle_get_system_info(buf, len);
        break;

    case ISO15693_CMD_READ_SINGLE_BLOCK:
        handle_read_single_block(buf, len);
        break;

    case ISO15693_CMD_WRITE_SINGLE_BLOCK:
        handle_write_single_block(buf, len);
        break;

    case ISO15693_CMD_READ_MULTIPLE_BLOCKS:
        handle_read_multiple_blocks(buf, len);
        break;

    case ISO15693_CMD_LOCK_BLOCK:
        handle_lock_block(buf, len);
        break;

    case ISO15693_CMD_WRITE_AFI:
        handle_write_afi(buf, len);
        break;

    case ISO15693_CMD_LOCK_AFI:
        handle_lock_afi(buf, len);
        break;

    case ISO15693_CMD_WRITE_DSFID:
        handle_write_dsfid(buf, len);
        break;

    case ISO15693_CMD_LOCK_DSFID:
        handle_lock_dsfid(buf, len);
        break;

    case ISO15693_CMD_GET_MULTI_BLOCK_SEC:
        handle_get_multi_block_sec(buf, len);
        break;

    default:
        ESP_LOGW(TAG, "Unsupported cmd 0x%02X", cmd_code);
        if (is_for_us(buf, len)) {
            tx_error(ISO15693_ERR_NOT_SUPPORTED);
        }
        break;
    }
}
