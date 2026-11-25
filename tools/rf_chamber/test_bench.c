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
#include <stdio.h>
#include <string.h>
#include "cc1101.h"
#include "cc1101_bench.h"
#include "rf_chamber.h"
static uint8_t regs[64], pa[8], fifo[64];
static int pins[32], directions[32];
static int64_t now, tx_start;
static bool cw, stuck_tx, bad_read;
static int error;
static unsigned writes;
const char *esp_err_to_name(int e) { (void)e; return "mock"; }
int64_t esp_timer_get_time(void) { return now; }
void vTaskDelay(int ticks) { now += ticks * 1000; }
int gpio_set_direction(int pin,int mode) { directions[pin]=mode; return 0; }
int gpio_set_level(int pin,int level) { pins[pin]=level; return 0; }
int gpio_input_enable(int pin) {(void)pin;return 0;}
int gpio_get_level(int pin) { return pins[pin]; }
esp_err_t cc1101_init(void) { return ESP_OK; }
bool cc1101_is_initialized(void) { return true; }
int cc1101_lease_take_error(void) { int e=error;error=0;return e; }
esp_err_t cc1101_write_reg(uint8_t reg,uint8_t value) {
  assert(reg!=CC1101_FSTEST && reg!=CC1101_PTEST && reg!=CC1101_AGCTEST);
  regs[reg]=value; ++writes; return ESP_OK;
}
uint8_t cc1101_read_reg(uint8_t reg) {
  if (reg==(CC1101_MARCSTATE|0x40)) {
    if (regs[CC1101_MARCSTATE]==19 && !cw && !stuck_tx && now-tx_start>40000) {
      regs[CC1101_MARCSTATE]=1; regs[CC1101_TXBYTES]=0;
    }
    return regs[CC1101_MARCSTATE];
  }
  if (reg == (CC1101_VERSION|0x40)) return 0x14;
  if (bad_read && reg==CC1101_IOCFG0) return regs[reg]^1;
  return regs[reg&0x3f];
}
esp_err_t cc1101_write_burst(uint8_t reg,const uint8_t *data,uint8_t len) {
  if (reg==CC1101_PATABLE) memcpy(pa,data,len);
  else if (reg==CC1101_TXFIFO) {memcpy(fifo,data,len);regs[CC1101_TXBYTES]=len;}
  else memcpy(regs+reg,data,len);
  ++writes;
  return ESP_OK;
}
void cc1101_read_burst(uint8_t reg,uint8_t *data,uint8_t len) {
  if (reg==CC1101_PATABLE) memcpy(data,pa,len);
  else if (reg==CC1101_RXFIFO) {memcpy(data,fifo,len);regs[CC1101_RXBYTES]=0;}
  else memcpy(data,regs+reg,len);
}
static bool completed_before_poll, dropped_stx, fifo_underflow;
esp_err_t cc1101_strobe(uint8_t command) {
  if (command==CC1101_SIDLE || command==CC1101_SCAL) regs[CC1101_MARCSTATE]=1;
  if (command==CC1101_SRX) regs[CC1101_MARCSTATE]=13;
  if (command==CC1101_STX && !dropped_stx) {regs[CC1101_MARCSTATE]=19;tx_start=now;cw=regs[CC1101_PKTCTRL0]==0x32;
    if(completed_before_poll)now+=50000;
    if(fifo_underflow)regs[CC1101_TXBYTES]=0x80;
  }
  if (command==CC1101_SFRX) regs[CC1101_RXBYTES]=0;
  if (command==CC1101_SFTX) regs[CC1101_TXBYTES]=0;
  return ESP_OK;
}
int main(void) {
  rf_chamber_config_t c={.radio=RF_CHAMBER_CC,.mode=RF_CHAMBER_CW,.frequency_hz=433920000,
    .power_dbm=-30,.path=1,.sf=7,.bandwidth_khz=125,.cr=1,.duration_ms=1000,.interval_ms=1000};
  assert(rf_chamber_validate(&c)==0);
  assert(rf_chamber_validate(NULL)!=0);
  uint32_t invalid[]={0,299999999,348000001,386999999,464000001,778999999,928000001,UINT32_MAX};
  for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {c.frequency_hz=invalid[i];assert(rf_chamber_validate(&c)!=0);}
  c.frequency_hz=433920000;c.duration_ms=0;assert(rf_chamber_validate(&c)!=0);
  c.duration_ms=10001;assert(rf_chamber_validate(&c)!=0);c.duration_ms=1000;
  c.mode=RF_CHAMBER_RX;c.duration_ms=120000;assert(rf_chamber_validate(&c)==0);
  c.duration_ms++;assert(rf_chamber_validate(&c)!=0);c.duration_ms=1000;
  for (int i=-128;i<=127;++i) {c.power_dbm=i;bool ok=i==-30||i==-20||i==-10||i==0||i==5||i==10;assert((rf_chamber_validate(&c)==0)==ok);}
  c.radio=RF_CHAMBER_SX;c.frequency_hz=868100000;c.path=0;c.power_dbm=22;c.mode=RF_CHAMBER_CW;
  assert(rf_chamber_validate(&c)==0);c.duration_ms=1001;assert(rf_chamber_validate(&c)!=0);
  c.mode=RF_CHAMBER_RX;assert(rf_chamber_validate(&c)==0);
  for (unsigned sf=0;sf<256;++sf) {c.sf=sf;assert((rf_chamber_validate(&c)==0)==(sf>=7&&sf<=12));}
  c.sf=7;c.bandwidth_khz=126;assert(rf_chamber_validate(&c)!=0);
  uint8_t payload[32];uint32_t seq;
  for (uint32_t i=0;i<10000;++i) {
    rf_chamber_payload(i*100000, payload);assert(rf_chamber_parse_payload(payload,32,&seq)&&seq==i*100000);
    payload[31]^=1;assert(!rf_chamber_parse_payload(payload,32,&seq));
  }
  assert(!rf_chamber_parse_payload(NULL,32,&seq));assert(!rf_chamber_parse_payload(payload,31,&seq));
  uint8_t saved[47]; for(unsigned i=0;i<47;++i) regs[i]=saved[i]=i;
  uint32_t hz[]={315000000,433920000,868350000,915000000};
  const uint8_t expected_pa[]={0x51,0x60,0x50,0x8e};
  for (unsigned band=0;band<4;++band) for (unsigned path=1;path<=4;++path) {
    cc1101_bench_config_t cfg={.frequency_hz=hz[band],.power_dbm=0,.path=path,.is_cw=true};
    assert(cc1101_bench_prepare(&cfg)==0);
    assert(pa[0]==0&&pa[1]==expected_pa[band]);assert(pins[17]==(int)((path-1)>>1)&&pins[18]==(int)((path-1)&1));
    assert(cc1101_bench_enter(true)==0);
    cc1101_bench_sample_t sample;assert(cc1101_bench_sample(&sample)==0&&sample.state==19);
    assert(cc1101_bench_packet(payload)!=0);
    assert(cc1101_bench_restore()==0);assert(memcmp(regs,saved,47)==0);assert(directions[8]==1);
  }
  cc1101_bench_config_t cfg={.frequency_hz=433920000,.power_dbm=-30,.path=1};
  assert(cc1101_bench_prepare(&cfg)==0);rf_chamber_payload(123,payload);
  assert(cc1101_bench_packet(payload)==0&&memcmp(fifo,payload,32)==0);
  completed_before_poll=true;assert(cc1101_bench_packet(payload)==0);completed_before_poll=false;
  dropped_stx=true;assert(cc1101_bench_packet(payload)==ESP_ERR_TIMEOUT);dropped_stx=false;
  fifo_underflow=true;assert(cc1101_bench_packet(payload)==ESP_ERR_INVALID_RESPONSE);fifo_underflow=false;
  stuck_tx=true;assert(cc1101_bench_packet(payload)==ESP_ERR_TIMEOUT);stuck_tx=false;
  bad_read=true;assert(cc1101_bench_restore()==ESP_ERR_INVALID_RESPONSE);
  assert(cc1101_bench_prepare(&cfg)!=0);bad_read=false;assert(cc1101_bench_restore()==0);
  unsigned before=writes;cfg.power_dbm=11;assert(cc1101_bench_prepare(&cfg)!=0&&writes==before);
  assert(cc1101_bench_prepare(NULL)!=0);
  puts("PASS: production config limits, 10000 payload vectors, four-band PA/path matrix, CC packet completion including missed TX window, rejected STX, FIFO underflow, timeout and retained restore retry");
}
