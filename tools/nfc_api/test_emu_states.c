// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "st25r3916_reg.h"
#include "st25r3916_cmd.h"
#define ESP_OK 0
#define HB_NFC_OK 0
#define HB_NFC_ERR_INTERNAL -1
#define RF_FRONTEND_NFC_13MHZ 0
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define pdMS_TO_TICKS(x) (x)
#define MFC_EMU_EVT_ACTIVATED 0
#define MFC_EMU_EVT_FIELD_LOST 1
/* CONSTANTS */
typedef int hb_nfc_err_t;
typedef int esp_err_t;
typedef struct { uint8_t main,timer,error,target,collision; } st25r3916_irq_status_t;
static st25r3916_irq_status_t s_pending_irq;
static struct {
  bool initialized, crypto_active;
  uint8_t pending_main;
  int crypto;
  int64_t last_activity_us;
  mfc_emu_state_t state;
  struct { int cycles,field_losses,unknown_cmds,nacks_sent; } stats;
} s_emu;
static uint8_t s_regs[256], s_frame[20], s_last_command;
static int s_frame_len, s_sleep, s_sense, s_auth, s_reads;
static bool s_bus_fail, s_auth_fail;
static bool rf_frontend_can_access(int mode) { (void)mode; return true; }
static int hb_nfc_spi_reg_read(uint8_t reg,uint8_t *out) {
  *out=s_regs[reg];
  if (reg == ST25R3916_REG_MAIN_INT || reg == ST25R3916_REG_TIMER_NFC_INT) s_regs[reg]=0;
  return s_bus_fail?-1:0;
}
static int hb_nfc_spi_reg_write(uint8_t reg,uint8_t val) { s_regs[reg]=val; return s_bus_fail?-1:0; }
static int hb_nfc_spi_reg_modify(uint8_t reg,uint8_t mask,uint8_t val) {
  s_regs[reg]=(s_regs[reg]&~mask)|(val&mask); return s_bus_fail?-1:0;
}
static int hb_nfc_spi_direct_cmd(uint8_t cmd) {
  s_last_command=cmd;
  s_sleep+=cmd==ST25R3916_CMD_GOTO_SLEEP; s_sense+=cmd==ST25R3916_CMD_GOTO_SENSE;
  return s_bus_fail?-1:0;
}
static int st25r3916_irq_read(st25r3916_irq_status_t *status) {
  memset(status,0,sizeof(*status));
  hb_nfc_spi_reg_read(ST25R3916_REG_MAIN_INT,&status->main);
  hb_nfc_spi_reg_read(ST25R3916_REG_TIMER_NFC_INT,&status->timer);
  hb_nfc_spi_reg_read(ST25R3916_REG_TARGET_INT,&status->target);
  return s_bus_fail?-1:0;
}
static bool hb_nfc_gpio_irq_level(void) { return true; }
static void vTaskDelay(int delay) { (void)delay; }
static int64_t esp_timer_get_time(void) { return 1; }
static void emit_event(int event) { (void)event; }
static void reset_crypto_state(void) { s_emu.crypto_active=false;s_emu.pending_main=0;memset(&s_pending_irq,0,sizeof(s_pending_irq)); }
static uint8_t crypto1_byte(int *state,int in,int encrypted) { (void)state;(void)in;(void)encrypted;return 0; }
static int fifo_rx(uint8_t *out,size_t max) { assert((size_t)s_frame_len<=max);memcpy(out,s_frame,s_frame_len);return s_frame_len; }
static mfc_emu_state_t handle_auth(uint8_t cmd,uint8_t block) {
  (void)cmd;(void)block;s_auth++;return s_auth_fail?MFC_EMU_STATE_ERROR:MFC_EMU_STATE_AUTHENTICATED;
}
static mfc_emu_state_t handle_halt(void) { reset_crypto_state();return MFC_EMU_STATE_HALTED; }
static mfc_emu_state_t handle_read(uint8_t block) { (void)block;s_reads++;return MFC_EMU_STATE_AUTHENTICATED; }
static mfc_emu_state_t handle_write_phase1(uint8_t block) { (void)block;return MFC_EMU_STATE_WRITE_PENDING; }
static mfc_emu_state_t handle_value_op_phase1(uint8_t cmd,uint8_t block) { (void)cmd;(void)block;return MFC_EMU_STATE_VALUE_PENDING; }
static mfc_emu_state_t handle_transfer(uint8_t block) { (void)block;return MFC_EMU_STATE_AUTHENTICATED; }
static mfc_emu_state_t handle_write_phase2(uint8_t *p,int n) { (void)p;(void)n;return MFC_EMU_STATE_AUTHENTICATED; }
static mfc_emu_state_t handle_value_op_phase2(uint8_t *p,int n) { (void)p;(void)n;return MFC_EMU_STATE_AUTHENTICATED; }
static int target_tx_ack_encrypted(int ack) { (void)ack; return HB_NFC_OK; }
static bool command_frame_valid(const uint8_t *cmd, size_t len) { (void)cmd; return len >= 2; }
/* PRODUCTION */
int main(void) {
  s_emu.initialized=true;
  for (int repeat=0;repeat<10;repeat++) {
    s_emu.state=MFC_EMU_STATE_LISTEN;
    s_regs[ST25R3916_REG_PASSIVE_TARGET_STS]=PTA_STATE_SELECTED;
    s_regs[ST25R3916_REG_MAIN_INT]=ST25R3916_IRQ_MAIN_RXE;
    s_frame[0]=MFC_CMD_AUTH_KEY_A;s_frame[1]=0;s_frame_len=2;
    assert(mfc_emu_run_step()==MFC_EMU_STATE_ACTIVATED);
    assert(mfc_emu_run_step()==MFC_EMU_STATE_AUTHENTICATED);
    assert(s_auth==repeat+1);
    s_emu.crypto_active=true;s_frame[0]=MFC_CMD_READ;
    s_regs[ST25R3916_REG_MAIN_INT]=ST25R3916_IRQ_MAIN_RXE;
    assert(mfc_emu_run_step()==MFC_EMU_STATE_AUTHENTICATED);
    assert(s_reads==repeat+1);
    s_frame[0]=MFC_CMD_HALT;
    s_regs[ST25R3916_REG_MAIN_INT]=ST25R3916_IRQ_MAIN_RXE;
    assert(mfc_emu_run_step()==MFC_EMU_STATE_HALTED);
    assert(mfc_emu_run_step()==MFC_EMU_STATE_LISTEN);
    assert(s_sleep==repeat+1 && s_sense==0);
    assert(s_last_command==ST25R3916_CMD_GOTO_SLEEP);
  }
  s_emu.state=MFC_EMU_STATE_ACTIVATED;s_auth_fail=true;s_frame[0]=MFC_CMD_AUTH_KEY_A;
  s_regs[ST25R3916_REG_MAIN_INT]=ST25R3916_IRQ_MAIN_RXE;
  assert(mfc_emu_run_step()==MFC_EMU_STATE_LISTEN && s_sense==1);
  assert(s_last_command==ST25R3916_CMD_GOTO_SENSE);
  s_emu.state=MFC_EMU_STATE_ACTIVATED;s_regs[ST25R3916_REG_TIMER_NFC_INT]=8;
  s_regs[ST25R3916_REG_AUX_DISPLAY]=0;
  assert(mfc_emu_run_step()==MFC_EMU_STATE_LISTEN && s_sense==2);
  s_emu.state=MFC_EMU_STATE_ERROR;s_bus_fail=true;
  assert(mfc_emu_run_step()==MFC_EMU_STATE_ERROR);
  puts("PASS: repeated selection/RX, HALT sleep, RF timeout recovery and fatal bus errors");
}
