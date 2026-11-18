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
#include <stddef.h>
#include <stdio.h>
typedef int esp_err_t;
typedef int rf_chamber_radio_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_INVALID_RESPONSE 2
#define ESP_ERR_TIMEOUT 3
#define SX1262_STDBY_RC 0
#define SX1262_STATE_STDBY_RC 2
#define SX1262_STATE_TX 5
#define SX1262_ANT_OFF 0
#define SX1262_ANT_TX 1
#define SX1262_OP_SET_STANDBY 0x80
#define SX1262_OP_SET_TX_CONTINUOUS_WAVE 0xd1
#define RF_CHAMBER_CC 0
#define RF_CHAMBER_SX 1
static bool s_has_radio, s_has_power, s_has_hal, s_is_sx_initialized, s_needs_reinit;
static int s_sx, s_config_storage;
static void *s_config=&s_config_storage;
static int state=2,chip_mode=2,force_mode,cmd_error,restore_error,power_error;
static int antenna, releases, pauses, deinit_calls;
static void set_antenna(void *ctx,int mode) {(void)ctx;antenna=mode;}
typedef struct {void *ctx;void (*set_antenna)(void *,int);} hal_t;
static hal_t hal={.set_antenna=set_antenna};
static hal_t *s_hal=&hal;
int sx1262_cmd_write(void *h,int op,void *data,int size) {
  (void)h;(void)data;(void)size;if(cmd_error)return cmd_error;
  chip_mode=op==SX1262_OP_SET_STANDBY?2:6;return 0;
}
int sx1262_get_status(uint8_t *out) {*out=(force_mode?force_mode:chip_mode)<<4;return 0;}
int sx1262_fsm_get_state(void) {return state;}
int sx1262_fsm_transition(int to) {if(to==state)return ESP_ERR_INVALID_STATE;state=to;return 0;}
int apply_workaround_w1(void) {return 0;}
int cc1101_lease_acquire(void) {return 0;}
int cc1101_bench_restore(void) {return restore_error;}
void cc1101_lease_pause(void) {++pauses;}
int cc1101_lease_release(void) {++releases;return 0;}
int lora_session_radio_release(void *p) {(void)p;++releases;return 0;}
int power_manager_no_sleep_release(void) {return power_error;}
int sx1262_deinit(void) {++deinit_calls;return 0;}
/* PRODUCTION */
int main(void) {
  assert(sx1262_radio_standby_checked()==0); /* Already in RC is success. */
  assert(sx1262_radio_lab_cw_start()==0&&state==5&&chip_mode==6&&antenna==1);
  force_mode=6;assert(sx1262_radio_standby_checked()==ESP_ERR_INVALID_RESPONSE&&antenna==1);
  force_mode=0;assert(sx1262_radio_standby_checked()==0&&state==2&&antenna==0);
  cmd_error=ESP_ERR_TIMEOUT;assert(sx1262_radio_lab_cw_start()==ESP_ERR_TIMEOUT);cmd_error=0;
  s_needs_reinit=true;assert(sx1262_radio_lab_cw_start()==ESP_ERR_INVALID_STATE);s_needs_reinit=false;
  s_has_radio=s_has_power=true;restore_error=ESP_ERR_TIMEOUT;
  assert(cleanup(RF_CHAMBER_CC)==ESP_ERR_TIMEOUT&&s_has_radio&&s_has_power&&releases==0&&pauses==1);
  restore_error=0;power_error=ESP_ERR_TIMEOUT;assert(cleanup(RF_CHAMBER_CC)==ESP_ERR_TIMEOUT&&s_has_radio);
  power_error=0;assert(cleanup(RF_CHAMBER_CC)==0&&!s_has_radio&&!s_has_power&&releases==1);
  s_has_radio=s_has_power=s_has_hal=s_is_sx_initialized=true;force_mode=6;
  assert(cleanup(RF_CHAMBER_SX)==ESP_ERR_INVALID_RESPONSE&&s_has_radio&&s_has_hal&&deinit_calls==0);
  force_mode=0;assert(cleanup(RF_CHAMBER_SX)==0&&!s_has_radio&&!s_has_hal&&deinit_calls==1&&releases==2);
  assert(cleanup(RF_CHAMBER_SX)==0&&releases==2);
  puts("PASS: SX1262 chip/software state distinction, checked CW stop, command failure, retained CC/SX ownership and cleanup retries");
}
