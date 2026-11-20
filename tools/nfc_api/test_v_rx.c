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

#include "hb_nfc_v_rx.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int GPIO;
static void (*handler)(void *);
static bool enabled, in_isr, level, overflow;
static int64_t now;
static size_t count, index_next;
static uint32_t edges[2048];
static bool levels[2048];
int gpio_install_isr_service(int flags) { (void)flags; return ESP_ERR_INVALID_STATE; }
int gpio_set_intr_type(int pin,int type) { assert(pin==10 && type==3); return 0; }
int gpio_isr_handler_add(int pin,void (*cb)(void *),void *ctx) {
  assert(pin==10 && !ctx);handler=cb;return 0;
}
int gpio_isr_handler_remove(int pin) { assert(pin==10 && !enabled);handler=NULL;return 0; }
int gpio_intr_enable(int pin) { assert(pin==10);enabled=true;return 0; }
int gpio_intr_disable(int pin) { assert(pin==10);enabled=false;return 0; }
int gpio_ll_get_level(void *gpio,int pin) { assert(gpio==&GPIO && pin==10);return level; }
const char *esp_err_to_name(int err) { (void)err; return "error"; }
int64_t esp_timer_get_time(void) {
  if (in_isr) return now;
  now++;
  if(enabled && overflow) {
    in_isr=true;
    for(unsigned i=0;i<300;i++){level=!level;handler(NULL);}
    in_isr=false;overflow=false;
  }
  if(enabled && index_next<count && now>=edges[index_next]) {
    level=levels[index_next++];in_isr=true;handler(NULL);in_isr=false;
  }
  return now;
}
void esp_rom_delay_us(uint32_t us) {
  for(unsigned i=0;i<us;i++) (void)esp_timer_get_time();
}
static uint32_t sample;
static bool previous;
static void group(uint8_t bits) {
  for(unsigned i=0;i<8;i++,sample++) {
    bool next=(bits>>i)&1;
    if(next!=previous) {
      edges[count]=100+(sample*12800+678)/1356;
      levels[count++]=next;previous=next;
    }
  }
}
static void reset(void) {now=0;count=index_next=sample=0;level=previous=false;overflow=false;}
int main(void) {
  uint8_t data[16];size_t length;bool slot;int64_t eof;
  assert(hb_nfc_v_rx_init(-1)==ESP_ERR_INVALID_ARG);
  assert(hb_nfc_v_rx_init(10)==0);
  for(unsigned slow=0;slow<2;slow++) {
    reset();group(slow?0x81:0x21);
    const uint8_t patterns[]={2,8,32,128};
    for(unsigned byte=0;byte<3;byte++) {
      for(unsigned j=0;j<(slow?64U:4U);j++)
        group(slow?(j==byte/4?patterns[byte%4]:0):patterns[(byte>>(2*j))&3]);
    }
    group(4);
    assert(hb_nfc_v_rx_capture(data,sizeof(data),&length,&slot,&eof)==0);
    assert(length==3 && !slot && data[0]==0 && data[1]==1 && data[2]==2 && eof>0 && !enabled);
  }
  reset();group(1);
  assert(hb_nfc_v_rx_capture(data,sizeof(data),&length,&slot,&eof)==0 && slot && !length);
  reset();overflow=true;
  assert(hb_nfc_v_rx_capture(data,sizeof(data),&length,&slot,&eof)==ESP_ERR_INVALID_SIZE);
  assert(!length && !slot && !eof && !enabled);
  reset();group(0xff);
  assert(hb_nfc_v_rx_capture(data,sizeof(data),&length,&slot,&eof)==ESP_ERR_INVALID_RESPONSE);
  reset();
  assert(hb_nfc_v_rx_capture(data,sizeof(data),&length,&slot,&eof)==ESP_ERR_TIMEOUT);
  assert(hb_nfc_v_rx_deinit()==0 && handler==NULL);
  assert(hb_nfc_v_rx_capture(data,sizeof(data),&length,&slot,&eof)==ESP_ERR_INVALID_STATE);
  puts("IRQ capture: both codings, long quiet gaps, slot EOF, overflow, noise, timeout and cleanup OK");
}
