#include <string.h>
#include "nfc_listener.h"
#include "nfc_device.h"
#include "mf_classic.h"
#include "mf_classic_emu.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "nfc_emu";

static TaskHandle_t  s_emu_task    = NULL;
static volatile bool s_emu_running = false;

/* Emulation loop — mfc_emu_run_step() handles every AUTH/READ/WRITE/VALUE
 * command from the reader. Hardware (PT memory) covers REQA/anticollision/SELECT.
 * taskYIELD() lets other tasks run without adding the ~10 ms latency that
 * vTaskDelay(1) would introduce at the default 100 Hz tick rate. */
static void nfc_listener_task(void* arg)
{
    (void)arg;
    ESP_LOGI(TAG, "emulation loop started");
    while (s_emu_running) {
        mfc_emu_run_step();
        taskYIELD();
    }
    ESP_LOGI(TAG, "emulation loop stopped");
    s_emu_task = NULL;
    vTaskDelete(NULL);
}

static hb_nfc_err_t start_emu_task(void)
{
    s_emu_running = true;
    BaseType_t rc = xTaskCreate(nfc_listener_task, "nfc_emu", 4096,
                                NULL, 6, &s_emu_task);
    return (rc == pdPASS) ? HB_NFC_OK : HB_NFC_ERR_INTERNAL;
}

hb_nfc_err_t nfc_listener_start(const hb_nfc_card_data_t* card)
{
    static mfc_emu_card_data_t emu;
    memset(&emu, 0, sizeof(emu));
    hb_nfc_err_t err;

    if (card && card->protocol == HB_PROTO_MF_CLASSIC) {
        mf_classic_type_t type = mf_classic_get_type(card->iso14443a.sak);
        mfc_emu_card_data_init(&emu, &card->iso14443a, type);
        err = mfc_emu_init(&emu);
    } else {
        int idx = nfc_device_get_active();
        if (idx < 0) return HB_NFC_ERR_PARAM;
        err = nfc_device_load(idx, &emu);
        if (err != HB_NFC_OK) return err;
        err = mfc_emu_init(&emu);
    }

    if (err != HB_NFC_OK) return err;
    err = mfc_emu_configure_target();
    if (err != HB_NFC_OK) return err;
    err = mfc_emu_start();
    if (err != HB_NFC_OK) return err;
    return start_emu_task();
}

hb_nfc_err_t nfc_listener_start_emu(const mfc_emu_card_data_t* emu)
{
    hb_nfc_err_t err = mfc_emu_init(emu);
    if (err != HB_NFC_OK) return err;
    err = mfc_emu_configure_target();
    if (err != HB_NFC_OK) return err;
    err = mfc_emu_start();
    if (err != HB_NFC_OK) return err;
    return start_emu_task();
}

void nfc_listener_stop(void)
{
    s_emu_running = false;
    while (s_emu_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    mfc_emu_stop();
}
