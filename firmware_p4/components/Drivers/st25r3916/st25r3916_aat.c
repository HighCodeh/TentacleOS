/**
 * @file st25r3916_aat.c
 * @brief ST25R3916 Antenna Auto-Tuning: 2-pass sweep + NVS cache.
 */
#include "st25r3916_aat.h"
#include "st25r3916_core.h"
#include "st25r3916_cmd.h"
#include "st25r3916_reg.h"
#include "hb_nfc_spi.h"
#include "hb_nfc_timer.h"
#include "esp_log.h"
#include "nvs.h"

static const char* TAG = "st25r_aat";

static hb_nfc_err_t aat_set(uint8_t dac_a, uint8_t dac_b)
{
    hb_nfc_err_t err = hb_spi_reg_write(REG_ANT_TUNE_A, dac_a);
    if (err != HB_NFC_OK) return err;
    return hb_spi_reg_write(REG_ANT_TUNE_B, dac_b);
}

static void aat_measure(uint8_t* amp, uint8_t* phase)
{
    hb_spi_direct_cmd(CMD_MEAS_AMPLITUDE);
    hb_delay_ms(2);
    hb_spi_reg_read(REG_AD_RESULT, amp);

    hb_spi_direct_cmd(CMD_MEAS_PHASE);
    hb_delay_ms(2);
    hb_spi_reg_read(REG_AD_RESULT, phase);
}

hb_nfc_err_t st25r_aat_calibrate(st25r_aat_result_t* result)
{
    if (!result) return HB_NFC_ERR_PARAM;

    bool was_on = st25r_field_is_on();
    if (!was_on) {
        hb_nfc_err_t err = st25r_field_on();
        if (err != HB_NFC_OK) return err;
    }

    uint8_t best_a = 0x80;
    uint8_t best_b = 0x80;
    uint8_t best_amp = 0;
    uint8_t best_phase = 0;
    const uint8_t coarse = 16;

    for (uint16_t a = 0; a <= 0xFF; a += coarse) {
        for (uint16_t b = 0; b <= 0xFF; b += coarse) {
            hb_nfc_err_t err = aat_set((uint8_t)a, (uint8_t)b);
            if (err != HB_NFC_OK) return err;
            uint8_t amp = 0, phase = 0;
            aat_measure(&amp, &phase);
            if (amp > best_amp) {
                best_amp = amp;
                best_phase = phase;
                best_a = (uint8_t)a;
                best_b = (uint8_t)b;
            }
        }
    }

    const uint8_t fine = 4;
    uint8_t start_a = (best_a > coarse) ? (best_a - coarse) : 0;
    uint8_t end_a   = (best_a + coarse < 0xFF) ? (best_a + coarse) : 0xFF;
    uint8_t start_b = (best_b > coarse) ? (best_b - coarse) : 0;
    uint8_t end_b   = (best_b + coarse < 0xFF) ? (best_b + coarse) : 0xFF;

    for (uint16_t a = start_a; a <= end_a; a += fine) {
        for (uint16_t b = start_b; b <= end_b; b += fine) {
            hb_nfc_err_t err = aat_set((uint8_t)a, (uint8_t)b);
            if (err != HB_NFC_OK) return err;
            uint8_t amp = 0, phase = 0;
            aat_measure(&amp, &phase);
            if (amp > best_amp) {
                best_amp = amp;
                best_phase = phase;
                best_a = (uint8_t)a;
                best_b = (uint8_t)b;
            }
        }
    }

    (void)aat_set(best_a, best_b);
    result->dac_a = best_a;
    result->dac_b = best_b;
    result->amplitude = best_amp;
    result->phase = best_phase;

    if (!was_on) {
        st25r_field_off();
    }

    ESP_LOGI(TAG, "AAT sweep: A=0x%02X B=0x%02X AMP=%u PH=%u",
             result->dac_a, result->dac_b, result->amplitude, result->phase);
    return HB_NFC_OK;
}

hb_nfc_err_t st25r_aat_load_nvs(st25r_aat_result_t* result)
{
    if (!result) return HB_NFC_ERR_PARAM;
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open("hb_aat", NVS_READONLY, &nvs);
    if (err != ESP_OK) return HB_NFC_ERR_INTERNAL;

    uint8_t a = 0, b = 0, amp = 0, ph = 0;
    if (nvs_get_u8(nvs, "dac_a", &a) != ESP_OK ||
        nvs_get_u8(nvs, "dac_b", &b) != ESP_OK ||
        nvs_get_u8(nvs, "amp",   &amp) != ESP_OK ||
        nvs_get_u8(nvs, "phase", &ph) != ESP_OK) {
        nvs_close(nvs);
        return HB_NFC_ERR_INTERNAL;
    }
    nvs_close(nvs);

    result->dac_a = a;
    result->dac_b = b;
    result->amplitude = amp;
    result->phase = ph;
    return HB_NFC_OK;
}

hb_nfc_err_t st25r_aat_save_nvs(const st25r_aat_result_t* result)
{
    if (!result) return HB_NFC_ERR_PARAM;
    nvs_handle_t nvs = 0;
    esp_err_t err = nvs_open("hb_aat", NVS_READWRITE, &nvs);
    if (err != ESP_OK) return HB_NFC_ERR_INTERNAL;

    if (nvs_set_u8(nvs, "dac_a", result->dac_a)    != ESP_OK ||
        nvs_set_u8(nvs, "dac_b", result->dac_b)    != ESP_OK ||
        nvs_set_u8(nvs, "amp",   result->amplitude) != ESP_OK ||
        nvs_set_u8(nvs, "phase", result->phase)     != ESP_OK) {
        nvs_close(nvs);
        return HB_NFC_ERR_INTERNAL;
    }
    err = nvs_commit(nvs);
    nvs_close(nvs);
    return (err == ESP_OK) ? HB_NFC_OK : HB_NFC_ERR_INTERNAL;
}
