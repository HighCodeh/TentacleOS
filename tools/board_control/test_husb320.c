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
#include "../../firmware_p4/components/Drivers/husb320/husb320.c"

static uint8_t regs[8];
static int calls, fail_at, removed, writes;
static bool wrong_readback;
static int step(void) { return ++calls==fail_at ? ESP_FAIL : ESP_OK; }
void *i2c_get_bus(void) { return (void *)1; }
int i2c_master_bus_add_device(void *bus,const i2c_device_config_t *cfg,void **out) {
  assert(bus && cfg->device_address==0x42 && cfg->scl_speed_hz==100000);
  *out=(void *)2; return ESP_OK;
}
int i2c_master_bus_rm_device(void *dev) { assert(dev); removed++; return ESP_OK; }
int i2c_master_transmit_receive(void *dev,const uint8_t *in,size_t n,uint8_t *out,size_t m,int timeout) {
  assert(dev && n==1 && m==1 && timeout==100 && *in<8);
  if(step()!=ESP_OK) return ESP_FAIL;
  *out=regs[*in]; if(wrong_readback && *in==3) *out=4; return ESP_OK;
}
int i2c_master_transmit(void *dev,const uint8_t *in,size_t n,int timeout) {
  assert(dev && n==2 && timeout==100 && in[0]<8);
  if(step()!=ESP_OK) return ESP_FAIL;
  if(in[0]==3) assert(!(regs[5]&8));
  if(in[0]==5 && (in[1]&8)) assert((regs[3]&0x3f)==2);
  regs[in[0]]=in[1]; writes++; return ESP_OK;
}
static void reset(void) { regs[2]=3; regs[3]=0x4c; regs[5]=0x23; calls=removed=writes=0; fail_at=0; wrong_readback=false; }
int main(void) {
  reset(); assert(husb320_init()==ESP_OK && regs[3]==0x42 && regs[5]==0x2b && removed==1);
  int total=calls;
  for(int i=1;i<=total;i++) { reset(); fail_at=i; assert(husb320_init()!=ESP_OK && removed==1); }
  reset(); regs[2]=0; assert(husb320_init()==ESP_ERR_NOT_SUPPORTED && writes==0 && removed==1);
  reset(); wrong_readback=true; assert(husb320_init()==ESP_ERR_INVALID_RESPONSE && !(regs[5]&8));
  return 0;
}
