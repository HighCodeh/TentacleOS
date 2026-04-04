#include "nfc_manager.h"
#include "nfc_reader.h"
#include "st25r3916_core.h"
#include "nfc_poller.h"
#include "poller.h"
#include "hb_nfc_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static struct {
    nfc_state_t state;
    nfc_card_found_cb_t cb;
    void* ctx;
    TaskHandle_t task;
    bool running;
} s_mgr = { 0 };

static void nfc_manager_auto_handle(const hb_nfc_card_data_t* card)
{
    if (!card) return;

    switch (card->protocol) {
    case HB_PROTO_MF_CLASSIC:
        mf_classic_read_full((nfc_iso14443a_data_t*)&card->iso14443a);
        break;
    case HB_PROTO_MF_ULTRALIGHT:
        mful_dump_card((nfc_iso14443a_data_t*)&card->iso14443a);
        break;
    case HB_PROTO_MF_PLUS:
        mfp_probe_and_dump((nfc_iso14443a_data_t*)&card->iso14443a);
        break;
    case HB_PROTO_ISO14443_4A:
        t4t_dump_ndef((nfc_iso14443a_data_t*)&card->iso14443a);
        break;
    default:
        break;
    }
}

static void nfc_manager_task(void* arg)
{
    (void)arg;
    s_mgr.state = NFC_STATE_SCANNING;

    while (s_mgr.running) {
        nfc_iso14443a_data_t card = { 0 };
        hb_nfc_err_t err = iso14443a_poller_select(&card);

        if (err == HB_NFC_OK) {
            s_mgr.state = NFC_STATE_READING;

            hb_nfc_card_data_t full = { 0 };
            full.protocol = HB_PROTO_ISO14443_3A;
            full.iso14443a = card;

            if (card.sak == 0x00) {
                full.protocol = HB_PROTO_MF_ULTRALIGHT;
            } else if (card.sak == 0x08 || card.sak == 0x18 ||
                       card.sak == 0x09 || card.sak == 0x88) {
                full.protocol = HB_PROTO_MF_CLASSIC;
            } else if (card.sak == 0x10 || card.sak == 0x11) {
                /* MIFARE Plus SL2 — backward-compatible with MIFARE Classic */
                full.protocol = HB_PROTO_MF_CLASSIC;
            } else if (card.sak == 0x20) {
                /* ISO-DEP: could be DESFire, MIFARE Plus SL3, JCOP, etc.
                 * Route to MFP probe — it falls through to T4T if not MFP. */
                full.protocol = HB_PROTO_MF_PLUS;
            } else if (card.sak & 0x20) {
                full.protocol = HB_PROTO_ISO14443_4A;
            }

            if (s_mgr.cb) {
                s_mgr.cb(&full, s_mgr.ctx);
            } else {
                nfc_manager_auto_handle(&full);
            }

            s_mgr.state = NFC_STATE_SCANNING;
        }

        hb_delay_ms(100);
    }

    s_mgr.state = NFC_STATE_IDLE;
    s_mgr.task = NULL;
    vTaskDelete(NULL);
}

hb_nfc_err_t nfc_manager_start(nfc_card_found_cb_t cb, void* ctx)
{
    /* Hardware must already be initialized via highboy_nfc_init().
     * This function only applies the RF config and starts the scan task. */
    hb_nfc_err_t err = nfc_poller_start();
    if (err != HB_NFC_OK) return err;

    s_mgr.cb = cb;
    s_mgr.ctx = ctx;
    s_mgr.running = true;

    xTaskCreate(nfc_manager_task, "nfc_mgr", 8192, NULL, 5, &s_mgr.task);
    return HB_NFC_OK;
}

void nfc_manager_stop(void)
{
    s_mgr.running = false;
    while (s_mgr.task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    /* Turn off field but keep SPI/GPIO alive so emulation can start next. */
    nfc_poller_stop();
}

nfc_state_t nfc_manager_get_state(void)
{
    return s_mgr.state;
}
