// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "script_engine.h"
#include "script_bindings.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char s_output_text[8192];
static size_t s_output_length;
static unsigned s_polls;
static unsigned s_stop_after;
static unsigned s_allocations;
static unsigned s_fail_after;
static uint32_t s_slept;

static void output(void *ctx, const char *text, size_t length) {
  (void)ctx;
  assert(length < sizeof(s_output_text) - s_output_length);
  memcpy(s_output_text + s_output_length, text, length);
  s_output_length += length;
  s_output_text[s_output_length] = 0;
}
static bool poll(void *ctx) { (void)ctx; return ++s_polls >= s_stop_after; }
static void sleep_ms(void *ctx, uint32_t milliseconds) { (void)ctx; s_slept += milliseconds; }
static int64_t clock_ms(void *ctx) { (void)ctx; return 42; }
static void *allocate(size_t length) {
  if (++s_allocations >= s_fail_after) return NULL;
  return malloc(length);
}
static script_engine_host_t host = {
  .output=output, .poll=poll, .sleep=sleep_ms, .clock=clock_ms, .alloc=allocate
};
static script_engine_result_t run(script_engine_language_t language, const char *source) {
  s_output_length = 0; s_output_text[0] = 0; s_polls = 0; s_allocations = 0; s_slept = 0;
  script_engine_request_t request = {.language=language, .source=source, .length=strlen(source),
                                    .name="host-test", .host=&host};
  return script_engine_run(&request);
}
static unsigned s_json_calls;
static unsigned s_json_fail;
static void *json_allocate(size_t size) { if(++s_json_calls==s_json_fail) return NULL; return malloc(size); }
static int should_stop(void) { return 0; }
static unsigned s_scans;
static unsigned s_closed;
static unsigned s_keys;
static esp_err_t scan(void) { ++s_scans; return ESP_OK; }
static esp_err_t ap_get(int index,tos_wifi_ap_t *out_ap) {
  if(index!=0) return ESP_ERR_NOT_FOUND;
  *out_ap=(tos_wifi_ap_t){.ssid="example",.bssid={0,1,2,3,4,5},.channel=6,.rssi=-42};
  return ESP_OK;
}
static void *open_file(const char *path) { return strcmp(path,"/sdcard/test")==0 ? (void *)1 : NULL; }
static int read_file(void *file,void *buffer,int count) {
  assert(file==(void *)1); if(count<3) return -1;
  memcpy(buffer,"a\0b",3); return 3;
}
static void close_file(void *file) { assert(file==(void *)1); ++s_closed; }
static esp_err_t send_key(uint8_t modifier,uint8_t key) { assert(modifier==0 && key==4); ++s_keys; return ESP_OK; }
static unsigned s_rf_reads;
static esp_err_t read_rfid(tos_rfid_tag_t *out,uint32_t timeout) {
  assert(timeout==500); ++s_rf_reads;
  *out=(tos_rfid_tag_t){.id="0102030405",.protocol="EM4100",.bit_count=40,
    .raw_value=UINT64_C(0x0102030405),.card_number=0x02030405};
  return ESP_OK;
}
static esp_err_t poll_nfc(tos_nfc_tag_t *out,uint32_t timeout) {
  assert(timeout==500); ++s_rf_reads;
  *out=(tos_nfc_tag_t){.uid={1,2,3,4},.uid_len=4,.atqa={0x44,0},.sak=0x20,
    .protocol=TOS_NFC_ISO14443A};
  return ESP_OK;
}
static esp_err_t receive_ir(tos_ir_code_t *out, uint32_t timeout) {
  assert(timeout==500); *out=(tos_ir_code_t){.protocol=TOS_IR_NEC,.address=42,.command=7,.repeat=false};return ESP_OK;
}
static esp_err_t send_ir(const tos_ir_code_t *code) {
  assert(code->protocol==TOS_IR_NEC && code->address==42 && code->command==7 && !code->repeat);return ESP_OK;
}
static unsigned s_view_calls;
static int s_view_count=1;
static int poll_subghz_view(tos_subghz_signal_t *out) {
  ++s_view_calls;
  *out=(tos_subghz_signal_t){.protocol="CAME",.decoded=true,.freq_hz=433920000,.value=42};
  return s_view_count;
}
static int poll_mesh_view(tos_lora_msg_t *out) {
  ++s_view_calls;
  *out=(tos_lora_msg_t){.who="peer",.text="hello"};
  return s_view_count;
}
static unsigned s_subghz_calls;
static esp_err_t capture_subghz(tos_subghz_signal_t *out,uint32_t freq,uint8_t preset,uint32_t timeout) {
  assert(freq==433920000 && preset==6 && timeout==500);++s_subghz_calls;
  *out=(tos_subghz_signal_t){.protocol="CAME",.decoded=true,.value=42};return ESP_OK;
}
static esp_err_t transmit_subghz(const int32_t *pulses,int count,uint32_t freq) {
  assert(freq==433920000 && count==3 && pulses[0]==350 && pulses[1]==-700 && pulses[2]==350);
  ++s_subghz_calls;return ESP_OK;
}
static unsigned s_spectrum_calls;
static esp_err_t s_spectrum_error;
static esp_err_t capture_spectrum(tos_subghz_spectrum_t *out,uint32_t center,uint32_t span) {
  assert(center==433920000 && span==1000000);++s_spectrum_calls;
  out->start_hz=433420000;out->step_hz=12500;
  for(unsigned i=0;i<TOS_SUBGHZ_SPECTRUM_BINS;++i)out->dbm[i]=-100.0f+(float)i;
  return s_spectrum_error;
}
static const tos_subghz_api_t subghz={.rx_poll=poll_subghz_view,.receive=capture_subghz,.tx_raw=transmit_subghz,.spectrum_capture=capture_spectrum};
static unsigned s_mesh_sends;
static esp_err_t s_mesh_error;
static esp_err_t send_mesh(const char *text) {
  assert(strcmp(text,"hello")==0);++s_mesh_sends;return s_mesh_error;
}
static const tos_lora_api_t lora={.mesh_poll=poll_mesh_view,.mesh_send_text=send_mesh};
static unsigned s_ir_extended_calls;
static esp_err_t send_ir_raw(const int32_t *pulses,int count,uint32_t carrier) {
 assert(carrier==38000 && count==3 && pulses[0]==350 && pulses[1]==-700 && pulses[2]==350);
 ++s_ir_extended_calls;return ESP_OK;
}
static int get_ir_raw(int32_t *out,int max) {
 assert(max==3);out[0]=350;out[1]=-700;out[2]=350;++s_ir_extended_calls;return 3;
}
static esp_err_t send_ir_file(const char *path) {
 assert(strcmp(path,"/sdcard/one.ir")==0);++s_ir_extended_calls;return ESP_OK;
}
static esp_err_t send_ir_ac(const tos_ir_ac_t *state) {
 assert(state->protocol==TOS_IR_AC_HAIER && state->mode==TOS_IR_AC_MODE_HEAT && state->power && state->temp_c==23 && state->fan==TOS_IR_AC_FAN_HIGH);
 ++s_ir_extended_calls;return ESP_OK;
}
static const tos_ir_api_t ir={.receive=receive_ir,.send=send_ir,.send_raw=send_ir_raw,.get_last_raw=get_ir_raw,.send_file=send_ir_file,.send_ac=send_ir_ac};
static const tos_rfid_api_t rfid={.read=read_rfid};
static unsigned s_nfc_blocks;
static esp_err_t read_nfc_block(uint8_t block,const uint8_t key[6],uint8_t data[16]) {
 assert(block==4 && key[0]==0xFF && key[5]==0xFF);memset(data,0xCA,16);++s_nfc_blocks;return ESP_OK;
}
static esp_err_t write_nfc_block(uint8_t block,const uint8_t key[6],const uint8_t data[16]) {
 assert(block==4 && key[0]==0xFF && key[5]==0xFF && data[0]==0xCA && data[15]==0xCA);++s_nfc_blocks;return ESP_OK;
}
static const tos_nfc_api_t nfc={.poll=poll_nfc,.read_block=read_nfc_block,.write_block=write_nfc_block};
static const tos_wifi_api_t wifi={.scan=scan,.ap_get=ap_get};
static const tos_ble_api_t ble={.hid_send_key=send_key};
static const tos_fs_api_t fs={.open=open_file,.read=read_file,.close=close_file};
static const tos_api_t api={.abi_major=1,.abi_minor=TOS_ABI_VERSION_MINOR,.should_stop=should_stop,.wifi=&wifi,.ble=&ble,.fs=&fs,.nfc=&nfc,.rfid=&rfid,.ir=&ir,.subghz=&subghz,.lora=&lora};
static script_bindings_t bindings={.api=&api};

static void test_bindings(void) {
  uint32_t caps;
  assert(script_bindings_requested_caps("// @caps 0x09\n",&caps)==ESP_OK && caps==9);
  assert(script_bindings_requested_caps("-- @caps 0x02\n",&caps)==ESP_ERR_NOT_SUPPORTED);
  assert(script_bindings_requested_caps("// @caps 0x100000000\n",&caps)==ESP_ERR_INVALID_ARG);
  assert(script_bindings_requested_caps("// @caps 0x08garbage\n",&caps)==ESP_ERR_INVALID_ARG);
  assert(script_bindings_requested_caps("// @caps 0x08 \n",&caps)==ESP_ERR_INVALID_ARG);
  assert(run(SCRIPT_ENGINE_JS,"tos.call('version')")==SCRIPT_ENGINE_ERROR);
  host.bindings=&bindings;
  for(int language=0;language<SCRIPT_ENGINE_LANGUAGE_COUNT;++language) {
    assert(run(language,"print(tos.call('version').major)")==SCRIPT_ENGINE_OK);
    assert(strcmp(s_output_text,"1\n")==0);
    assert(run(language,language==SCRIPT_ENGINE_JS ? "if(tos.call('should_stop')!==false) throw new Error('bool');" : "assert(not tos.call('should_stop'))")==SCRIPT_ENGINE_OK);
    bindings.caps=0;
    unsigned scans=s_scans;
    assert(run(language,"tos.call('wifi.scan')")==SCRIPT_ENGINE_ERROR);
    assert(s_scans==scans);
    assert(run(language,"tos.call('no.such.method')")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('fs.open','/sdcard/test')")==SCRIPT_ENGINE_ERROR);
    unsigned spectrum_calls=s_spectrum_calls;
    assert(run(language,"tos.call('subghz.spectrum_capture',433920000,1000000)")==SCRIPT_ENGINE_ERROR);
    assert(spectrum_calls==s_spectrum_calls);
    unsigned mesh_sends=s_mesh_sends;
    assert(run(language,"tos.call('lora.mesh_send_text','hello')")==SCRIPT_ENGINE_ERROR);
    assert(mesh_sends==s_mesh_sends);
    unsigned subghz_calls=s_subghz_calls;
    assert(run(language,"tos.call('subghz.receive',433920000,6,500)")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('subghz.tx_raw',433920000,'350,-700,350')")==SCRIPT_ENGINE_ERROR);
    assert(subghz_calls==s_subghz_calls);
    unsigned views=s_view_calls;
    assert(run(language,"tos.call('subghz.rx_poll')")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('lora.mesh_poll')")==SCRIPT_ENGINE_ERROR);
    assert(s_view_calls==views);
    unsigned rf_reads=s_rf_reads;
    assert(run(language,"tos.call('nfc.poll',500)")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('rfid.read',500)")==SCRIPT_ENGINE_ERROR);
    assert(s_rf_reads==rf_reads);
    bindings.caps=SCRIPT_BINDINGS_CAPS;
    assert(run(language,"tos.call('lora.mesh_send_text','hello')")==SCRIPT_ENGINE_OK);
    assert(s_mesh_sends==mesh_sends+1);
    s_mesh_error=ESP_ERR_TIMEOUT;
    assert(run(language,"tos.call('lora.mesh_send_text','hello')")==SCRIPT_ENGINE_ERROR);
    s_mesh_error=ESP_OK;
    tos_api_t old_mesh_api=api;old_mesh_api.abi_minor=13;bindings.api=&old_mesh_api;
    mesh_sends=s_mesh_sends;
    assert(run(language,"tos.call('lora.mesh_send_text','hello')")==SCRIPT_ENGINE_ERROR);
    assert(mesh_sends==s_mesh_sends);bindings.api=&api;
    assert(run(language,language==SCRIPT_ENGINE_JS
      ? "var x=tos.call('subghz.spectrum_capture',433920000,1000000);if(x.start_hz!==433420000 || x.step_hz!==12500 || x.dbm.length!==80 || x.dbm[79]!==-21) throw new Error('spectrum');"
      : "local x=tos.call('subghz.spectrum_capture',433920000,1000000);assert(x.start_hz==433420000 and x.step_hz==12500 and #x.dbm==80 and x.dbm[80]==-21)")==SCRIPT_ENGINE_OK);
    s_spectrum_error=ESP_ERR_TIMEOUT;
    assert(run(language,"tos.call('subghz.spectrum_capture',433920000,1000000)")==SCRIPT_ENGINE_ERROR);
    s_spectrum_error=ESP_OK;
    tos_api_t old_spectrum_api=api;old_spectrum_api.abi_minor=14;bindings.api=&old_spectrum_api;
    spectrum_calls=s_spectrum_calls;
    assert(run(language,"tos.call('subghz.spectrum_capture',433920000,1000000)")==SCRIPT_ENGINE_ERROR);
    assert(spectrum_calls==s_spectrum_calls);bindings.api=&api;
    s_view_count=1;
    assert(run(language,"print(tos.call('subghz.receive',433920000,6,500).value)")==SCRIPT_ENGINE_OK);
    assert(strcmp(s_output_text,"42\n")==0);
    assert(run(language,"tos.call('subghz.tx_raw',433920000,'350,-700,350')")==SCRIPT_ENGINE_OK);
    const char *bad_pulses[]={"", "0", "350,", "350,,700", "350 700", "32768", "-32768", "2147483648", "x", " 350", "+", "-"};
    subghz_calls=s_subghz_calls;
    for(unsigned bad=0;bad<sizeof(bad_pulses)/sizeof(bad_pulses[0]);++bad) {
      char code[256];snprintf(code,sizeof(code),"tos.call('subghz.tx_raw',433920000,'%s')",bad_pulses[bad]);
      assert(run(language,code)==SCRIPT_ENGINE_ERROR);
    }
    assert(subghz_calls==s_subghz_calls);
    tos_api_t old_capture_api=api;old_capture_api.abi_minor=12;bindings.api=&old_capture_api;
    assert(run(language,"tos.call('subghz.receive',433920000,6,500)")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('subghz.tx_raw',433920000,'350,-700,350')")==SCRIPT_ENGINE_ERROR);
    assert(subghz_calls==s_subghz_calls);bindings.api=&api;
    assert(run(language,"print(tos.call('subghz.rx_poll').value)")==SCRIPT_ENGINE_OK);
    assert(strcmp(s_output_text,"42\n")==0);
    assert(run(language,"print(tos.call('lora.mesh_poll').text)")==SCRIPT_ENGINE_OK);
    assert(strcmp(s_output_text,"hello\n")==0);
    s_view_count=0;
    assert(run(language,language==SCRIPT_ENGINE_JS
      ? "if(tos.call('subghz.rx_poll')!==null) throw new Error('empty');"
      : "assert(tos.call('subghz.rx_poll')==nil)")==SCRIPT_ENGINE_OK);
    assert(run(language,language==SCRIPT_ENGINE_JS
      ? "if(tos.call('lora.mesh_poll')!==null) throw new Error('empty');"
      : "assert(tos.call('lora.mesh_poll')==nil)")==SCRIPT_ENGINE_OK);
    s_view_count=-ESP_ERR_TIMEOUT;
    assert(run(language,"tos.call('subghz.rx_poll')")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('lora.mesh_poll')")==SCRIPT_ENGINE_ERROR);
    tos_api_t old_view_api=api; old_view_api.abi_minor=11; bindings.api=&old_view_api;
    views=s_view_calls;
    assert(run(language,"tos.call('subghz.rx_poll')")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('lora.mesh_poll')")==SCRIPT_ENGINE_ERROR);
    assert(s_view_calls==views); bindings.api=&api;
    assert(run(language,"print(tos.call('rfid.read',500).id)")==SCRIPT_ENGINE_OK);
    assert(strcmp(s_output_text,"0102030405\n")==0);
    assert(run(language,language==SCRIPT_ENGINE_JS
      ? "print(tos.call('nfc.poll',500).uid[0])" : "print(tos.call('nfc.poll',500).uid[1])")==SCRIPT_ENGINE_OK);
    assert(strcmp(s_output_text,"1\n")==0 && s_rf_reads==rf_reads+2);
    assert(run(language,"print(tos.call('ir.receive',500).address)")==SCRIPT_ENGINE_OK);
    assert(strcmp(s_output_text,"42\n")==0);
    bindings.caps &= ~TOS_CAP_RADIO_TX;
    assert(run(language,"tos.call('ir.send',0,42,7,false)")==SCRIPT_ENGINE_ERROR);
    bindings.caps |= TOS_CAP_RADIO_TX;
    assert(run(language,"tos.call('ir.send',0,42,7,false)")==SCRIPT_ENGINE_OK);
    unsigned extended=s_ir_extended_calls;
    assert(run(language,"tos.call('ir.send_raw',38000,'350,-700,350')")==SCRIPT_ENGINE_OK);
    assert(run(language,language==SCRIPT_ENGINE_JS ? "print(tos.call('ir.get_last_raw',3)[1])" : "print(tos.call('ir.get_last_raw',3)[2])")==SCRIPT_ENGINE_OK);
    assert(strcmp(s_output_text,"-700\n")==0);
    assert(run(language,"tos.call('ir.send_ac',5,true,2,23,3)")==SCRIPT_ENGINE_OK);
    bindings.caps &= ~TOS_CAP_FS_READ;
    assert(run(language,"tos.call('ir.send_file','/sdcard/one.ir')")==SCRIPT_ENGINE_ERROR);
    bindings.caps |= TOS_CAP_FS_READ;
    assert(run(language,"tos.call('ir.send_file','/sdcard/one.ir')")==SCRIPT_ENGINE_OK);
    assert(s_ir_extended_calls==extended+4);
    assert(run(language,"tos.call('ir.send_raw',38000,'350,')")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('ir.get_last_raw',129)")==SCRIPT_ENGINE_ERROR);
    tos_api_t old_extended_api=api;old_extended_api.abi_minor=15;bindings.api=&old_extended_api;
    assert(run(language,"tos.call('ir.send_ac',5,true,2,23,3)")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('ir.send_raw',38000,'350,-700,350')")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('ir.get_last_raw',3)")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('ir.send_file','/sdcard/one.ir')")==SCRIPT_ENGINE_ERROR);
    assert(s_ir_extended_calls==extended+4);bindings.api=&api;
    unsigned blocks=s_nfc_blocks;
    assert(run(language,language==SCRIPT_ENGINE_JS ? "print(tos.call('nfc.read_block',4,'FFFFFFFFFFFF')[0])" : "print(tos.call('nfc.read_block',4,'ffffffffffff')[1])")==SCRIPT_ENGINE_OK);
    assert(strcmp(s_output_text,"202\n")==0);
    assert(run(language,"tos.call('nfc.write_block',4,'FFFFFFFFFFFF','CACACACACACACACACACACACACACACACA')")==SCRIPT_ENGINE_OK);
    assert(run(language,"tos.call('nfc.read_block',4,'FF')")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('nfc.read_block',4,'GGFFFFFFFFFF')")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('nfc.write_block',4,'FFFFFFFFFFFF','CA')")==SCRIPT_ENGINE_ERROR);
    bindings.caps &= ~TOS_CAP_RADIO_TX;
    assert(run(language,"tos.call('nfc.write_block',4,'FFFFFFFFFFFF','CACACACACACACACACACACACACACACACA')")==SCRIPT_ENGINE_ERROR);
    bindings.caps |= TOS_CAP_RADIO_TX;
    tos_api_t old_block_api=api;old_block_api.abi_minor=16;bindings.api=&old_block_api;
    assert(run(language,"tos.call('nfc.read_block',4,'FFFFFFFFFFFF')")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('nfc.write_block',4,'FFFFFFFFFFFF','CACACACACACACACACACACACACACACACA')")==SCRIPT_ENGINE_ERROR);
    assert(s_nfc_blocks==blocks+2);bindings.api=&api;
    tos_api_t old_ir_api=api; old_ir_api.abi_minor=10; bindings.api=&old_ir_api;
    assert(run(language,"tos.call('ir.receive',500)")==SCRIPT_ENGINE_ERROR);
    bindings.api=&api;
    tos_api_t old_api=api; old_api.abi_minor=9; bindings.api=&old_api;
    assert(run(language,"tos.call('nfc.poll',500)")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('rfid.read',500)")==SCRIPT_ENGINE_ERROR);
    assert(s_rf_reads==rf_reads+2); bindings.api=&api;

    assert(run(language,"tos.call('wifi.scan'); print(tos.call('wifi.ap_get',0).bssid)")==SCRIPT_ENGINE_OK);
    assert(s_scans==scans+1 && strcmp(s_output_text,"00:01:02:03:04:05\n")==0);
    assert(run(language,"tos.call('wifi.ap_get',1)")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('wifi.ap_get',0.5)")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('ble.hid_send_key',0,256)")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('ble.hid_send_key',0,4)")==SCRIPT_ENGINE_OK);
    assert(run(language,"tos.call('wifi.sniffer_start',0,0,1)")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('wifi.scan',1)")==SCRIPT_ENGINE_ERROR);
    const char *read_script=language==SCRIPT_ENGINE_JS ?
      "var f=tos.call('fs.open','/sdcard/test'); var b=tos.call('fs.read',f,3); if(b.length!==3 || b[1]!==0) throw new Error('binary'); tos.call('fs.close',f);" :
      "local f=tos.call('fs.open','/sdcard/test'); local b=tos.call('fs.read',f,3); assert(#b==3 and b[2]==0); tos.call('fs.close',f)";
    assert(run(language,read_script)==SCRIPT_ENGINE_OK);
    unsigned closed=s_closed;
    assert(run(language,"tos.call('fs.open','/sdcard/test')")==SCRIPT_ENGINE_OK);
    script_bindings_close(&bindings);
    assert(s_closed==closed+1);
    assert(run(language,"tos.call('fs.close',1)")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('fs.read',4294967296,1)")==SCRIPT_ENGINE_ERROR);
    assert(run(language,"tos.call('fs.close',0)")==SCRIPT_ENGINE_ERROR);
  }
  assert(s_keys==SCRIPT_ENGINE_LANGUAGE_COUNT);
  cJSON_Hooks hooks={.malloc_fn=json_allocate,.free_fn=free};
  cJSON_InitHooks(&hooks);
  for(unsigned fail=1;fail<32;++fail) {
    for(int language=0;language<SCRIPT_ENGINE_LANGUAGE_COUNT;++language) {
      s_json_calls=0; s_json_fail=fail;
      script_engine_result_t result=run(language,"print(tos.call('wifi.ap_get',0).ssid)");
      assert(result==SCRIPT_ENGINE_OK || result==SCRIPT_ENGINE_ERROR);
      if(result==SCRIPT_ENGINE_OK) assert(strcmp(s_output_text,"example\n")==0);
    }
  }
  for(unsigned fail=1;fail<180;++fail) {
    for(int language=0;language<SCRIPT_ENGINE_LANGUAGE_COUNT;++language) {
      s_json_calls=0;s_json_fail=fail;
      script_engine_result_t result=run(language,"tos.call('subghz.spectrum_capture',433920000,1000000)");
      assert(result==SCRIPT_ENGINE_OK || result==SCRIPT_ENGINE_ERROR);
    }
  }
  cJSON_InitHooks(NULL);
  host.bindings=NULL;
}

int main(void) {
  s_stop_after = 1000000; s_fail_after = 1000000;
  assert(script_engine_run(NULL) == SCRIPT_ENGINE_INVALID_ARG);
  assert(run(SCRIPT_ENGINE_JS, "function twice(x){return x*2;} var a=[2,3]; console.log(JSON.stringify({answer:twice(a[1])})); sleep(10); print(millis());") == SCRIPT_ENGINE_OK);
  assert(strcmp(s_output_text, "{\"answer\":6}\n42\n") == 0 && s_slept == 10);
  assert(run(SCRIPT_ENGINE_LUA, "local t={2,3}; local function twice(x) return x*2 end; print(twice(t[2])); sleep(10); print(millis())") == SCRIPT_ENGINE_OK);
  assert(strcmp(s_output_text, "6\n42\n") == 0 && s_slept == 10);
  assert(run(SCRIPT_ENGINE_JS, "function {") == SCRIPT_ENGINE_ERROR);
  assert(run(SCRIPT_ENGINE_LUA, "local =") == SCRIPT_ENGINE_ERROR);
  assert(run(SCRIPT_ENGINE_JS, "throw new Error('example');") == SCRIPT_ENGINE_ERROR);
  assert(run(SCRIPT_ENGINE_LUA, "error('example')") == SCRIPT_ENGINE_ERROR);
  assert(run(SCRIPT_ENGINE_LUA, "assert(io==nil and os==nil and package==nil and debug==nil and loadfile==nil and load==nil)") == SCRIPT_ENGINE_OK);
  s_stop_after = 5;
  assert(run(SCRIPT_ENGINE_JS, "while(true){}") == SCRIPT_ENGINE_STOPPED);
  assert(run(SCRIPT_ENGINE_JS, "while(true){try{while(true){}}catch(e){}}") == SCRIPT_ENGINE_STOPPED);
  assert(run(SCRIPT_ENGINE_LUA, "while true do end") == SCRIPT_ENGINE_STOPPED);
  assert(run(SCRIPT_ENGINE_LUA, "while true do pcall(function() while true do end end) end") == SCRIPT_ENGINE_STOPPED);
  s_stop_after = 1;
  assert(run(SCRIPT_ENGINE_JS, "print('never')") == SCRIPT_ENGINE_STOPPED);
  assert(s_output_length == 0);
  s_stop_after = 1000000;
  assert(run(SCRIPT_ENGINE_JS, "sleep(-1)") == SCRIPT_ENGINE_ERROR);
  assert(run(SCRIPT_ENGINE_LUA, "sleep(30001)") == SCRIPT_ENGINE_ERROR);
  assert(run(SCRIPT_ENGINE_JS, "sleep(NaN)") == SCRIPT_ENGINE_ERROR);
  assert(run(SCRIPT_ENGINE_JS, "sleep(4294967296)") == SCRIPT_ENGINE_ERROR);
  assert(run(SCRIPT_ENGINE_LUA, "local function f() assert(pcall(f)) end; f()") == SCRIPT_ENGINE_ERROR);
  assert(run(SCRIPT_ENGINE_JS, "var a=[]; while(true){a.push('0123456789abcdef'+a.length);}") == SCRIPT_ENGINE_ERROR);
  assert(run(SCRIPT_ENGINE_LUA, "local x=string.rep('x',600000)") == SCRIPT_ENGINE_NO_MEMORY);
  assert(run(SCRIPT_ENGINE_LUA, "for i=1,500 do local t={}; for j=1,50 do t[j]=j end; for j=1,50 do t[j]=nil end; t.key=i end; print('churn')") == SCRIPT_ENGINE_OK);
  assert(strcmp(s_output_text,"churn\n") == 0);
  for (unsigned i = 1; i < 200; ++i) {
    s_fail_after = i;
    script_engine_result_t result = run(SCRIPT_ENGINE_LUA, "print('hello')");
    assert(result == SCRIPT_ENGINE_NO_MEMORY || result == SCRIPT_ENGINE_OK);
  }
  s_fail_after = 1;
  assert(run(SCRIPT_ENGINE_JS, "print('hello')") == SCRIPT_ENGINE_NO_MEMORY);
  s_fail_after = 1000000;
  assert(run(SCRIPT_ENGINE_JS, "print('recovered')") == SCRIPT_ENGINE_OK);
  assert(strcmp(s_output_text,"recovered\n") == 0);
  assert(run(SCRIPT_ENGINE_LUA, "print('recovered')") == SCRIPT_ENGINE_OK);
  assert(strcmp(s_output_text,"recovered\n") == 0);
  test_bindings();
  puts("PASS: real JS/Lua evaluation, output, syntax/runtime errors, cancellation, memory failures and recovery");
}
