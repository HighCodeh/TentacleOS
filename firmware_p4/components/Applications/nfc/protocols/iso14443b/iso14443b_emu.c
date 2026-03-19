/**
 * @file iso14443b_emu.c
 * @brief ISO14443B (NFC-B) target emulation with ISO-DEP/T4T APDUs.
 */
#include "iso14443b_emu.h"

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "st25r3916_core.h"
#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"
#include "st25r3916_fifo.h"
#include "st25r3916_irq.h"
#include "hb_nfc_spi.h"
#include "mf_desfire_emu.h"

static const char* TAG = "iso14443b_emu";

#define MODE_TARGET_NFCB   0x90U
#define OP_CTRL_TARGET     0xC3U

#define TIMER_I_EON        0x10U

#define ISO14443B_CMD_REQB   0x05U
#define ISO14443B_CMD_ATTRIB 0x1DU
#define ISO14443B_CMD_HLTB   0x50U

#define T4T_CC_LEN      15
#define T4T_NDEF_MAX    128
#define T4T_FILE_MAX    (2 + T4T_NDEF_MAX)
#define T4T_ML          0x0050U /* 80 bytes */
#define T4T_NDEF_FID    0xE104U
#define T4T_CC_FID      0xE103U

typedef enum {
    ISO14443B_STATE_SLEEP,
    ISO14443B_STATE_SENSE,
    ISO14443B_STATE_ACTIVE,
} iso14443b_state_t;

typedef enum {
    T4T_FILE_NONE = 0,
    T4T_FILE_CC,
    T4T_FILE_NDEF,
} t4t_file_t;

static iso14443b_state_t s_state = ISO14443B_STATE_SLEEP;
static bool        s_init_done = false;
static bool        s_activated = false;

static bool        s_app_selected = false;
static t4t_file_t  s_file = T4T_FILE_NONE;

static iso14443b_emu_card_t s_card;

static uint8_t  s_cc[T4T_CC_LEN];
static uint8_t  s_ndef_file[T4T_FILE_MAX];

static uint8_t  s_last_resp[260];
static int      s_last_resp_len = 0;

static uint8_t  s_chain_buf[300];
static size_t   s_chain_len = 0;
static bool     s_chain_active = false;
static uint8_t  s_pcd_cid = 0;

static TickType_t s_sense_tick = 0;
static TickType_t s_active_tick = 0;

static bool wait_oscillator(int timeout_ms)
{
    for (int i = 0; i < timeout_ms; i++) {
        uint8_t aux = 0, mi = 0, ti = 0;
        hb_spi_reg_read(REG_AUX_DISPLAY, &aux);
        hb_spi_reg_read(REG_MAIN_INT,    &mi);
        hb_spi_reg_read(REG_TARGET_INT,  &ti);
        if (((aux & 0x14) != 0) || ((mi & 0x80) != 0) || ((ti & 0x08) != 0)) {
            ESP_LOGI(TAG, "Osc OK in %dms: AUX=0x%02X MAIN=0x%02X TGT=0x%02X",
                     i, aux, mi, ti);
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    ESP_LOGW(TAG, "Osc timeout - continuing");
    return false;
}

static void build_cc(void)
{
    /* CC file: 15 bytes */
    s_cc[0]  = 0x00; s_cc[1]  = 0x0F; /* CCLEN */
    s_cc[2]  = 0x20;                /* Mapping Version 2.0 */
    s_cc[3]  = (uint8_t)((T4T_ML >> 8) & 0xFF);
    s_cc[4]  = (uint8_t)(T4T_ML & 0xFF);  /* MLe */
    s_cc[5]  = (uint8_t)((T4T_ML >> 8) & 0xFF);
    s_cc[6]  = (uint8_t)(T4T_ML & 0xFF);  /* MLc */
    s_cc[7]  = 0x04;                /* NDEF File Control TLV */
    s_cc[8]  = 0x06;
    s_cc[9]  = (uint8_t)((T4T_NDEF_FID >> 8) & 0xFF);
    s_cc[10] = (uint8_t)(T4T_NDEF_FID & 0xFF);
    s_cc[11] = 0x00;
    s_cc[12] = (uint8_t)T4T_NDEF_MAX; /* max size */
    s_cc[13] = 0x00; /* read access */
    s_cc[14] = 0x00; /* write access */
}

static void build_ndef_text(const char* text)
{
    memset(s_ndef_file, 0x00, sizeof(s_ndef_file));

    if (!text) text = "High Boy NFC T4T-B";
    size_t tl = strlen(text);
    size_t max_text = (T4T_NDEF_MAX > 7) ? (T4T_NDEF_MAX - 7) : 0;
    if (tl > max_text) tl = max_text;
    size_t pl = 1 + 2 + tl;            /* status + lang(2) + text */

    size_t pos = 2;
    s_ndef_file[pos++] = 0xD1;         /* MB/ME/SR + TNF=1 */
    s_ndef_file[pos++] = 0x01;         /* type length */
    s_ndef_file[pos++] = (uint8_t)pl;  /* payload length */
    s_ndef_file[pos++] = 'T';
    s_ndef_file[pos++] = 0x02;         /* UTF-8 + lang len=2 */
    s_ndef_file[pos++] = 'p';
    s_ndef_file[pos++] = 't';

    memcpy(&s_ndef_file[pos], text, tl);
    pos += tl;

    uint16_t nlen = (uint16_t)(pos - 2);
    s_ndef_file[0] = (uint8_t)((nlen >> 8) & 0xFF);
    s_ndef_file[1] = (uint8_t)(nlen & 0xFF);
}

hb_nfc_err_t iso14443b_emu_init_default(void)
{
    build_cc();
    build_ndef_text("High Boy NFC T4T-B");

    /* Default ATQB content */
    s_card.pupi[0] = 0x11; s_card.pupi[1] = 0x22;
    s_card.pupi[2] = 0x33; s_card.pupi[3] = 0x44;
    memset(s_card.app_data, 0x00, sizeof(s_card.app_data));
    s_card.prot_info[0] = 0x80; /* FSCI=8 (FSC=256), other bits 0 */
    s_card.prot_info[1] = 0x00;
    s_card.prot_info[2] = 0x00;

    s_state = ISO14443B_STATE_SLEEP;
    s_init_done = true;
    s_activated = false;
    s_app_selected = false;
    s_file = T4T_FILE_NONE;
    s_chain_active = false;
    s_chain_len = 0;
    s_pcd_cid = 0;
    s_sense_tick = 0;
    s_active_tick = 0;
    ESP_LOGI(TAG, "ISO14443B init: NDEF max=%u", (unsigned)T4T_NDEF_MAX);
    return HB_NFC_OK;
}

hb_nfc_err_t iso14443b_emu_configure_target(void)
{
    if (!s_init_done) return HB_NFC_ERR_INTERNAL;

    hb_spi_reg_write(REG_OP_CTRL, 0x00);
    vTaskDelay(pdMS_TO_TICKS(5));
    hb_spi_direct_cmd(CMD_SET_DEFAULT);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t ic = 0;
    hb_spi_reg_read(REG_IC_IDENTITY, &ic);
    if (ic == 0x00 || ic == 0xFF) return HB_NFC_ERR_INTERNAL;

    hb_spi_reg_write(REG_IO_CONF2, 0x80);
    vTaskDelay(pdMS_TO_TICKS(2));

    hb_spi_reg_write(REG_OP_CTRL, 0x80);
    wait_oscillator(200);

    hb_spi_direct_cmd(CMD_ADJUST_REGULATORS);
    vTaskDelay(pdMS_TO_TICKS(10));

    hb_spi_reg_write(REG_MODE, MODE_TARGET_NFCB);
    hb_spi_reg_write(REG_AUX_DEF, 0x00);
    hb_spi_reg_write(REG_BIT_RATE, 0x00);
    hb_spi_reg_write(REG_ISO14443B, 0x00);
    hb_spi_reg_write(REG_ISO14443B_FELICA, 0x00);
    hb_spi_reg_write(REG_PASSIVE_TARGET, 0x00);

    hb_spi_reg_write(REG_FIELD_THRESH_ACT,   0x00);
    hb_spi_reg_write(REG_FIELD_THRESH_DEACT, 0x00);
    hb_spi_reg_write(REG_PT_MOD, 0x60);

    st25r_irq_read();
    hb_spi_reg_write(REG_MASK_MAIN_INT,      0x00);
    hb_spi_reg_write(REG_MASK_TIMER_NFC_INT, 0x00);
    hb_spi_reg_write(REG_MASK_ERROR_WUP_INT, 0x00);
    hb_spi_reg_write(REG_MASK_TARGET_INT,    0x00);

    ESP_LOGI(TAG, "ISO14443B target configured");
    return HB_NFC_OK;
}

hb_nfc_err_t iso14443b_emu_start(void)
{
    if (!s_init_done) return HB_NFC_ERR_INTERNAL;

    st25r_irq_read();

    hb_spi_reg_write(REG_OP_CTRL, OP_CTRL_TARGET);
    vTaskDelay(pdMS_TO_TICKS(5));

    hb_spi_direct_cmd(CMD_GOTO_SLEEP);
    vTaskDelay(pdMS_TO_TICKS(2));

    s_state = ISO14443B_STATE_SLEEP;
    ESP_LOGI(TAG, "ISO14443B emulation active - present a reader");
    return HB_NFC_OK;
}

void iso14443b_emu_stop(void)
{
    hb_spi_reg_write(REG_OP_CTRL, 0x00);
    s_state = ISO14443B_STATE_SLEEP;
    s_init_done = false;
    ESP_LOGI(TAG, "ISO14443B emulation stopped");
}

static void tx_with_crc(const uint8_t* data, int len)
{
    if (!data || len <= 0) return;
    st25r_fifo_clear();
    st25r_set_tx_bytes((uint16_t)len, 0);
    st25r_fifo_load(data, (size_t)len);
    hb_spi_direct_cmd(CMD_TX_WITH_CRC);
}

static void send_atqb(void)
{
    uint8_t atqb[11];
    memcpy(&atqb[0], s_card.pupi, 4);
    memcpy(&atqb[4], s_card.app_data, 4);
    memcpy(&atqb[8], s_card.prot_info, 3);
    tx_with_crc(atqb, sizeof(atqb));
}

static void send_attrib_ok(void)
{
    uint8_t resp = 0x00;
    tx_with_crc(&resp, 1);
}

static void send_i_block_resp(const uint8_t* inf, int inf_len, uint8_t pcb_in)
{
    uint8_t resp[300];
    int pos = 0;
    resp[pos++] = (uint8_t)(0x02U | (pcb_in & 0x01U));
    if (pcb_in & 0x08U) { /* CID present */
        resp[pos++] = s_pcd_cid;
    }
    if (inf_len > 0 && inf_len < (int)sizeof(resp) - 1) {
        memcpy(&resp[pos], inf, (size_t)inf_len);
        pos += inf_len;
    }
    tx_with_crc(resp, pos);
    if (pos < (int)sizeof(s_last_resp)) {
        memcpy(s_last_resp, resp, (size_t)pos);
        s_last_resp_len = pos;
    }
}

static void send_r_ack(uint8_t pcb_in)
{
    uint8_t resp[2];
    int pos = 0;
    resp[pos++] = (uint8_t)(0xA2U | (pcb_in & 0x01U));
    if (pcb_in & 0x08U) {
        resp[pos++] = s_pcd_cid;
    }
    tx_with_crc(resp, pos);
}

static void apdu_resp(uint8_t* out, int* out_len,
                      const uint8_t* data, int data_len, uint16_t sw)
{
    int pos = 0;
    if (data && data_len > 0) {
        memcpy(&out[pos], data, (size_t)data_len);
        pos += data_len;
    }
    out[pos++] = (uint8_t)((sw >> 8) & 0xFF);
    out[pos++] = (uint8_t)(sw & 0xFF);
    *out_len = pos;
}

static uint16_t file_len(t4t_file_t f)
{
    if (f == T4T_FILE_CC) return T4T_CC_LEN;
    if (f == T4T_FILE_NDEF) {
        uint16_t nlen = (uint16_t)((s_ndef_file[0] << 8) | s_ndef_file[1]);
        if (nlen > T4T_NDEF_MAX) nlen = T4T_NDEF_MAX;
        return (uint16_t)(nlen + 2);
    }
    return 0;
}

static uint8_t* file_ptr(t4t_file_t f)
{
    if (f == T4T_FILE_CC) return s_cc;
    if (f == T4T_FILE_NDEF) return s_ndef_file;
    return NULL;
}

static void handle_apdu(const uint8_t* apdu, int apdu_len, uint8_t pcb_in)
{
    uint8_t resp[260];
    int resp_len = 0;

    if (mf_desfire_emu_handle_apdu(apdu, apdu_len, resp, &resp_len)) {
        send_i_block_resp(resp, resp_len, pcb_in);
        return;
    }

    if (apdu_len < 4) {
        apdu_resp(resp, &resp_len, NULL, 0, 0x6700);
        send_i_block_resp(resp, resp_len, pcb_in);
        return;
    }

    uint8_t cla = apdu[0];
    uint8_t ins = apdu[1];
    uint8_t p1  = apdu[2];
    uint8_t p2  = apdu[3];
    (void)cla;

    if (ins == 0xA4) { /* SELECT */
        if (apdu_len < 5) { apdu_resp(resp, &resp_len, NULL, 0, 0x6700); }
        else {
            uint8_t lc = apdu[4];
            if (apdu_len < 5 + lc) {
                apdu_resp(resp, &resp_len, NULL, 0, 0x6700);
            } else if (p1 == 0x04 && lc == 7 &&
                       memcmp(&apdu[5], (const uint8_t[]){0xD2,0x76,0x00,0x00,0x85,0x01,0x01}, 7) == 0) {
                s_app_selected = true;
                s_file = T4T_FILE_NONE;
                apdu_resp(resp, &resp_len, NULL, 0, 0x9000);
            } else if (p1 == 0x00 && lc == 2) {
                uint16_t fid = (uint16_t)((apdu[5] << 8) | apdu[6]);
                if (fid == T4T_CC_FID) { s_file = T4T_FILE_CC; apdu_resp(resp, &resp_len, NULL, 0, 0x9000); }
                else if (fid == T4T_NDEF_FID) { s_file = T4T_FILE_NDEF; apdu_resp(resp, &resp_len, NULL, 0, 0x9000); }
                else { apdu_resp(resp, &resp_len, NULL, 0, 0x6A82); }
            } else {
                apdu_resp(resp, &resp_len, NULL, 0, 0x6A82);
            }
        }
        send_i_block_resp(resp, resp_len, pcb_in);
        return;
    }

    if (ins == 0xB0) { /* READ BINARY */
        if (apdu_len < 5) { apdu_resp(resp, &resp_len, NULL, 0, 0x6700); }
        else if (s_file == T4T_FILE_NONE || !s_app_selected) {
            apdu_resp(resp, &resp_len, NULL, 0, 0x6985);
        } else {
            uint16_t off = (uint16_t)((p1 << 8) | p2);
            uint16_t flen = file_len(s_file);
            uint8_t  le = apdu[4];
            uint16_t max_le = (le == 0) ? 256 : le;
            if (max_le > T4T_ML) {
                apdu_resp(resp, &resp_len, NULL, 0, (uint16_t)(0x6C00 | (T4T_ML & 0xFF)));
                send_i_block_resp(resp, resp_len, pcb_in);
                return;
            }
            if (off >= flen) {
                apdu_resp(resp, &resp_len, NULL, 0, 0x6B00);
            } else {
                uint16_t avail = (uint16_t)(flen - off);
                uint16_t rd = (avail < max_le) ? avail : max_le;
                uint8_t* fp = file_ptr(s_file);
                apdu_resp(resp, &resp_len, &fp[off], rd, 0x9000);
            }
        }
        send_i_block_resp(resp, resp_len, pcb_in);
        return;
    }

    if (ins == 0xD6) { /* UPDATE BINARY */
        if (apdu_len < 5) { apdu_resp(resp, &resp_len, NULL, 0, 0x6700); }
        else if (s_file != T4T_FILE_NDEF || !s_app_selected) {
            apdu_resp(resp, &resp_len, NULL, 0, 0x6985);
        } else {
            uint16_t off = (uint16_t)((p1 << 8) | p2);
            uint8_t lc = apdu[4];
            if (apdu_len < 5 + lc) {
                apdu_resp(resp, &resp_len, NULL, 0, 0x6700);
            } else if (lc > T4T_ML) {
                apdu_resp(resp, &resp_len, NULL, 0, 0x6700);
            } else if (off + lc > (uint16_t)sizeof(s_ndef_file)) {
                apdu_resp(resp, &resp_len, NULL, 0, 0x6B00);
            } else {
                memcpy(&s_ndef_file[off], &apdu[5], lc);
                apdu_resp(resp, &resp_len, NULL, 0, 0x9000);
            }
        }
        send_i_block_resp(resp, resp_len, pcb_in);
        return;
    }

    apdu_resp(resp, &resp_len, NULL, 0, 0x6A86);
    send_i_block_resp(resp, resp_len, pcb_in);
}

void iso14443b_emu_run_step(void)
{
    uint8_t tgt_irq   = 0;
    uint8_t main_irq  = 0;
    uint8_t timer_irq = 0;

    hb_spi_reg_read(REG_TARGET_INT,    &tgt_irq);
    hb_spi_reg_read(REG_MAIN_INT,      &main_irq);
    hb_spi_reg_read(REG_TIMER_NFC_INT, &timer_irq);

    if (s_state == ISO14443B_STATE_SLEEP) {
        bool wake = (timer_irq & TIMER_I_EON)
                 || (tgt_irq & IRQ_TGT_WU_A)
                 || (tgt_irq & IRQ_TGT_WU_F);
        if (wake) {
            ESP_LOGI(TAG, "SLEEP -> SENSE (tgt=0x%02X tmr=0x%02X)", tgt_irq, timer_irq);
            s_sense_tick = xTaskGetTickCount();
            s_active_tick = s_sense_tick;
            s_state = ISO14443B_STATE_SENSE;
            mf_desfire_emu_reset();
            hb_spi_direct_cmd(CMD_GOTO_SENSE);
            /* Re-read IRQs after state change (clears stale flags) */
            hb_spi_reg_read(REG_TARGET_INT,    &tgt_irq);
            hb_spi_reg_read(REG_MAIN_INT,      &main_irq);
            hb_spi_reg_read(REG_TIMER_NFC_INT, &timer_irq);
        }
        return;
    }

    if (s_state == ISO14443B_STATE_SENSE) {
        static uint32_t s_idle_log = 0;
        if (!(main_irq & IRQ_MAIN_FWL)) {
            TickType_t now = xTaskGetTickCount();
            if ((now - s_sense_tick) > pdMS_TO_TICKS(500)) {
                ESP_LOGI(TAG, "SENSE: idle timeout -> SLEEP");
                s_state = ISO14443B_STATE_SLEEP;
                mf_desfire_emu_reset();
                hb_spi_direct_cmd(CMD_GOTO_SLEEP);
            }
            if ((++s_idle_log % 200U) == 0U) {
                uint8_t fs1 = 0, aux = 0;
                hb_spi_reg_read(REG_FIFO_STATUS1, &fs1);
                hb_spi_reg_read(REG_AUX_DISPLAY, &aux);
                ESP_LOGI(TAG, "[SENSE] main=0x%02X tgt=0x%02X tmr=0x%02X fs1=0x%02X aux=0x%02X",
                         main_irq, tgt_irq, timer_irq, fs1, aux);
            }
            return;
        }

        uint8_t buf[64] = { 0 };
        uint8_t fs1 = 0;
        hb_spi_reg_read(REG_FIFO_STATUS1, &fs1);
        int n = fs1 & 0x7F;
        if (n <= 0) return;
        if (n > (int)sizeof(buf)) n = (int)sizeof(buf);
        hb_spi_fifo_read(buf, (uint8_t)n);

        if (buf[0] == ISO14443B_CMD_REQB && n >= 3) {
            ESP_LOGI(TAG, "REQB");
            send_atqb();
            return;
        }

        if (buf[0] == ISO14443B_CMD_ATTRIB && n >= 9) {
            if (memcmp(&buf[1], s_card.pupi, 4) != 0) {
                ESP_LOGW(TAG, "ATTRIB: PUPI mismatch");
                return;
            }
            s_pcd_cid = buf[8] & 0x0F;
            s_state = ISO14443B_STATE_ACTIVE;
            s_activated = true;
            s_active_tick = xTaskGetTickCount();
            ESP_LOGI(TAG, "ATTRIB OK (CID=%u)", (unsigned)s_pcd_cid);
            send_attrib_ok();
            return;
        }

        if (buf[0] == ISO14443B_CMD_HLTB && n >= 5) {
            if (memcmp(&buf[1], s_card.pupi, 4) == 0) {
                ESP_LOGI(TAG, "HLTB -> SLEEP");
                s_state = ISO14443B_STATE_SLEEP;
                s_activated = false;
                mf_desfire_emu_reset();
                hb_spi_direct_cmd(CMD_GOTO_SLEEP);
            }
            return;
        }

        return;
    }

    /* ACTIVE */
        if (!(main_irq & IRQ_MAIN_FWL)) goto idle_active;

    uint8_t buf[300] = { 0 };
    uint8_t fs1 = 0;
    hb_spi_reg_read(REG_FIFO_STATUS1, &fs1);
    int n = fs1 & 0x7F;
    if (n <= 0) return;
    if (n > (int)sizeof(buf)) n = (int)sizeof(buf);
    hb_spi_fifo_read(buf, (uint8_t)n);
    int len = n;

    if (len <= 0) return;

    if (!s_activated) return;

    uint8_t pcb = buf[0];
    if ((pcb & 0xC0U) == 0x80U) {
        if (s_last_resp_len > 0) {
            tx_with_crc(s_last_resp, s_last_resp_len);
        }
        return;
    }

    if ((pcb & 0xC0U) != 0x00U) {
        return;
    }

    bool chaining = (pcb & 0x10U) != 0U;
    int idx = 1;
    if (pcb & 0x08U) { /* CID present */
        s_pcd_cid = buf[idx];
        idx++;
    }
    if (pcb & 0x04U) { /* NAD present */
        idx++;
    }
    if (idx > len) return;
    const uint8_t* inf = &buf[idx];
    int inf_len = len - idx;

    if (chaining) {
        if (!s_chain_active) {
            s_chain_active = true;
            s_chain_len = 0;
        }
        if (s_chain_len + (size_t)inf_len < sizeof(s_chain_buf)) {
            memcpy(&s_chain_buf[s_chain_len], inf, (size_t)inf_len);
            s_chain_len += (size_t)inf_len;
        }
        send_r_ack(pcb);
        return;
    }

    if (s_chain_active) {
        if (s_chain_len + (size_t)inf_len < sizeof(s_chain_buf)) {
            memcpy(&s_chain_buf[s_chain_len], inf, (size_t)inf_len);
            s_chain_len += (size_t)inf_len;
        }
        ESP_LOGI(TAG, "APDU (chained) len=%u", (unsigned)s_chain_len);
        handle_apdu(s_chain_buf, (int)s_chain_len, pcb);
        s_chain_active = false;
        s_chain_len = 0;
        return;
    }

    ESP_LOGI(TAG, "APDU len=%d", inf_len);
    handle_apdu(inf, inf_len, pcb);
    s_active_tick = xTaskGetTickCount();
    return;

idle_active:
    {
        TickType_t now = xTaskGetTickCount();
        if ((now - s_active_tick) > pdMS_TO_TICKS(2000)) {
            ESP_LOGI(TAG, "ACTIVE: idle timeout -> SLEEP");
            s_state = ISO14443B_STATE_SLEEP;
            s_activated = false;
            s_app_selected = false;
            s_file = T4T_FILE_NONE;
            s_chain_active = false;
            s_chain_len = 0;
            s_pcd_cid = 0;
            mf_desfire_emu_reset();
            hb_spi_direct_cmd(CMD_GOTO_SLEEP);
        }
    }
}
