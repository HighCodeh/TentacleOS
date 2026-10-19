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
static const tos_wifi_api_t wifi={.scan=scan,.ap_get=ap_get};
static const tos_ble_api_t ble={.hid_send_key=send_key};
static const tos_fs_api_t fs={.open=open_file,.read=read_file,.close=close_file};
static const tos_api_t api={.abi_major=1,.abi_minor=9,.should_stop=should_stop,.wifi=&wifi,.ble=&ble,.fs=&fs};
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
    bindings.caps=SCRIPT_BINDINGS_CAPS;
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
