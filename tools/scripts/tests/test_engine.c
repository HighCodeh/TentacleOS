// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "script_engine.h"
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
static const script_engine_host_t host = {
  .output=output, .poll=poll, .sleep=sleep_ms, .clock=clock_ms, .alloc=allocate
};
static script_engine_result_t run(script_engine_language_t language, const char *source) {
  s_output_length = 0; s_output_text[0] = 0; s_polls = 0; s_allocations = 0; s_slept = 0;
  script_engine_request_t request = {.language=language, .source=source, .length=strlen(source),
                                    .name="host-test", .host=&host};
  return script_engine_run(&request);
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
  puts("PASS: real JS/Lua evaluation, output, syntax/runtime errors, cancellation, memory failures and recovery");
}
