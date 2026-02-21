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

#include "st25r3916_irq.h"
#include "st25r3916_reg.h"
#include "hb_nfc_spi.h"
#include "hb_nfc_timer.h"

#include "esp_log.h"

static const char* TAG = "st25r_irq";

st25r_irq_status_t st25r_irq_read(void)
{
    st25r_irq_status_t s = { 0 };
    hb_spi_reg_read(REG_ERROR_INT,     &s.error);
    hb_spi_reg_read(REG_TIMER_NFC_INT, &s.timer);
    hb_spi_reg_read(REG_MAIN_INT,      &s.main);
    hb_spi_reg_read(REG_TARGET_INT,    &s.target);
    hb_spi_reg_read(REG_COLLISION,      &s.collision);
    return s;
}

void st25r_irq_log(const char* ctx, uint16_t fifo_count)
{
    st25r_irq_status_t s = st25r_irq_read();
    ESP_LOGW(TAG, " %s IRQ: MAIN=0x%02X ERR=0x%02X TMR=0x%02X TGT=0x%02X COL=0x%02X FIFO=%u",
             ctx, s.main, s.error, s.timer, s.target, s.collision, fifo_count);
}

bool st25r_irq_wait_txe(void)
{
    for (int i = 0; i < 400; i++) {
        uint8_t irq;
        hb_spi_reg_read(REG_MAIN_INT, &irq);
        if (irq & IRQ_MAIN_TXE) return true;
        hb_delay_us(50);
    }
    ESP_LOGW(TAG, "TX timeout");
    return false;
}
