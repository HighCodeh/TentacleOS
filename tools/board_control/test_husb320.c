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

int locked;
static uint8_t regs[0x17];
static bool change_status;
static uint8_t wire_address;
static bool force_wrong_address;
static int calls, fail_at, removed, writes;
static bool wrong_readback;
static int step(void) { return ++calls==fail_at ? ESP_FAIL : ESP_OK; }
void *i2c_get_bus(void) { return (void *)1; }
int i2c_master_bus_add_device(void *bus,const i2c_device_config_t *cfg,void **out) {
  assert(bus && cfg->dev_addr_length==I2C_ADDR_BIT_LEN_7 && cfg->scl_speed_hz==100000);
  wire_address=(force_wrong_address?0x42:cfg->device_address)<<1;
  *out=(void *)2; return ESP_OK;
}
int i2c_master_bus_rm_device(void *dev) { assert(dev); removed++; return ESP_OK; }
int i2c_master_transmit_receive(void *dev,const uint8_t *in,size_t n,uint8_t *out,size_t m,int timeout) {
  assert(dev && n==1 && m==1 && timeout==15 && *in<sizeof(regs));
  if(wire_address!=0x42) return ESP_ERR_INVALID_RESPONSE; // HUSB320 write address on the wire
  if(step()!=ESP_OK) return ESP_FAIL;
  *out=regs[*in]; if(change_status && *in==0x11) regs[*in]^=2; if(wrong_readback && *in==3) *out=4; return ESP_OK;
}
int i2c_master_transmit(void *dev,const uint8_t *in,size_t n,int timeout) {
  assert(dev && n==2 && timeout==15 && in[0]<sizeof(regs));
  if(wire_address!=0x42) return ESP_ERR_INVALID_RESPONSE; // HUSB320 write address on the wire
  if(step()!=ESP_OK) return ESP_FAIL;
  if(in[0]==3) assert(!(regs[5]&8));
  if(in[0]==5 && (in[1]&8)) assert((regs[3]&0x3f)==2);
  regs[in[0]]=in[1]; writes++; return ESP_OK;
}
static void reset(void) { s_device=NULL;s_lock=NULL;s_is_ready=false;s_init_error=ESP_ERR_INVALID_STATE;s_last_reg=0;force_wrong_address=false;locked=0;change_status=false; regs[2]=3; regs[3]=0x4c; regs[5]=0x23; calls=removed=writes=0; fail_at=0; wrong_readback=false; }
int main(void) {
  reset(); assert(husb320_init()==ESP_OK && regs[3]==0x42 && regs[5]==0x2b && removed==0);
  int total=calls;
  for(int i=1;i<=total;i++) { reset(); fail_at=i; assert(husb320_init()!=ESP_OK && removed==0); husb320_source_t invalid;assert(husb320_read_source(&invalid)==ESP_FAIL && invalid.last_reg!=0); }
  reset();force_wrong_address=true;
  assert(husb320_init()==ESP_ERR_INVALID_RESPONSE && writes==0);
  husb320_source_t absent;
  assert(husb320_read_source(&absent)==ESP_ERR_INVALID_RESPONSE && absent.last_reg==0x02 && !absent.is_attached && !absent.advertised_ma);
  force_wrong_address=false;s_device=NULL;
  assert(husb320_init()==ESP_OK && wire_address==0x42);
  reset(); regs[2]=0; assert(husb320_init()==ESP_ERR_NOT_SUPPORTED && writes==0 && removed==0);
  reset(); wrong_readback=true; assert(husb320_init()==ESP_ERR_INVALID_RESPONSE && !(regs[5]&8));
  reset();assert(husb320_init()==ESP_OK);
  husb320_source_t source;
  regs[0x11]=0;regs[0x13]=0;
  assert(husb320_read_source(&source)==ESP_OK && !source.is_attached && source.advertised_ma==0);
  regs[0x13]=0x10;
  for(unsigned orient=0x10;orient<=0x20;orient+=0x10) {
    for(unsigned level=2;level<=6;level+=2) {
      regs[0x11]=orient|8|1|level;
      assert(husb320_read_source(&source)==ESP_OK && source.is_attached);
      assert(source.advertised_ma==(level==2?0:level==4?1500:3000));
    }
  }
  int before=calls;assert(husb320_read_source(&source)==ESP_OK);int transfers=calls-before;
  for(int i=1;i<=transfers;i++) {
    fail_at=calls+i;assert(husb320_read_source(&source)!=ESP_OK);
    assert(!source.is_attached && !source.advertised_ma && !locked);fail_at=0;
  }
  change_status=true;assert(husb320_read_source(&source)==ESP_ERR_INVALID_RESPONSE);change_status=false;
  regs[0x11]=0x1d;regs[0x13]=0x30;
  assert(husb320_read_source(&source)==ESP_ERR_INVALID_RESPONSE); // debug accessory
  regs[0x13]=0x10;regs[0x11]=0x3d;
  assert(husb320_read_source(&source)==ESP_ERR_INVALID_RESPONSE); // reserved orientation
  regs[0x11]=0x5d;assert(husb320_read_source(&source)==ESP_ERR_INVALID_RESPONSE); // contradictory VBUS
  regs[0x11]=0x15;assert(husb320_read_source(&source)==ESP_ERR_INVALID_RESPONSE); // VBUS absent
  regs[0x11]=0x19;assert(husb320_read_source(&source)==ESP_ERR_INVALID_RESPONSE); // no Rp
  regs[0x11]=0x1d;regs[5]&=~8;
  assert(husb320_read_source(&source)==ESP_ERR_INVALID_STATE);
  assert(husb320_init()==ESP_OK && husb320_read_source(&source)==ESP_OK);
  regs[3]=4;assert(husb320_read_source(&source)==ESP_ERR_INVALID_STATE);
  assert(husb320_init()==ESP_OK && husb320_read_source(&source)==ESP_OK);
  assert(husb320_read_source(NULL)==ESP_ERR_INVALID_ARG);
  locked=1;assert(husb320_read_source(&source)==ESP_ERR_TIMEOUT);locked=0;
  return 0;
}
