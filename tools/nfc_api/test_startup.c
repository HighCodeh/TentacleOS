#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"
typedef int hb_nfc_err_t;
#define HB_NFC_OK 0
#define HB_NFC_ERR_INTERNAL -1
#define ESP_OK 0
#define RF_FRONTEND_NFC_13MHZ 2
#define MFC_EMU_STATE_LISTEN 1
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_LOGE(tag, ...) ((void)(tag))
/* CONSTANTS */
static const char *TAG="TEST";
static struct { bool initialized;struct { uint8_t uid_len; } card;int state;int64_t last_activity_us; } s_emu;
typedef struct { unsigned main; } st25r3916_irq_status_t;
static unsigned s_calls,s_fail_at;
static bool s_can_access=true;
static int operation(void) { ++s_calls;return s_calls==s_fail_at?-1:0; }
static bool rf_frontend_can_access(int mode) { assert(mode==2);return s_can_access; }
static int hb_nfc_spi_reg_write(uint8_t reg,uint8_t value) { (void)reg;(void)value;return operation(); }
static int hb_nfc_spi_reg_read(uint8_t reg,uint8_t *out) {
 *out=reg==ST25R3916_REG_IO_CONF2?ST25R3916_IO_CONF2_SUP3V:0x4D;return operation();
}
static int hb_nfc_spi_reg_modify(uint8_t reg,uint8_t mask,uint8_t value) { (void)reg;(void)mask;(void)value;return operation(); }
static int hb_nfc_spi_direct_cmd(uint8_t cmd) { (void)cmd;return operation(); }
static int st25r3916_irq_read(st25r3916_irq_status_t *status) { (void)status;return operation(); }
static hb_nfc_err_t load_pt_memory(void) { return operation(); }
static void vTaskDelay(unsigned ms) { (void)ms; }
static int64_t esp_timer_get_time(void) { return 123; }
/* STARTUP */
int main(void) {
 s_emu.initialized=true;s_emu.card.uid_len=4;
 assert(mfc_emu_configure_target()==HB_NFC_OK);unsigned configure_calls=s_calls;
 for(unsigned i=1;i<=configure_calls;++i) { s_calls=0;s_fail_at=i;assert(mfc_emu_configure_target()!=HB_NFC_OK); }
 s_calls=0;s_fail_at=0;assert(mfc_emu_start()==HB_NFC_OK);unsigned start_calls=s_calls;
 for(unsigned i=1;i<=start_calls;++i) { s_calls=0;s_fail_at=i;assert(mfc_emu_start()!=HB_NFC_OK); }
 s_calls=0;s_can_access=false;assert(mfc_emu_configure_target()!=HB_NFC_OK && mfc_emu_start()!=HB_NFC_OK && s_calls==0);
 s_can_access=true;s_fail_at=0;s_emu.card.uid_len=7;assert(mfc_emu_configure_target()==HB_NFC_OK && mfc_emu_start()==HB_NFC_OK);
 puts("PASS: actual NFC target configure/listen/start propagate every injected SPI/IRQ failure");return 0;
}
