// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void (*s_pending_task)(void *);
static int s_task_should_fail;
static int s_finished_tasks;
static int64_t s_now_us;
static const char *s_temporary_directory;
static FILE *open_script(const char *path, const char *mode) {
  const char *name = strrchr(path, '/');
  assert(name != NULL);
  char actual[512];
  snprintf(actual, sizeof(actual), "%s%s", s_temporary_directory, name);
  return fopen(actual, mode);
}
#define fopen open_script
#include "script_runner.c"
#undef fopen

static unsigned s_bindings;
static unsigned s_releases;
static bool s_bind_should_fail;
static int s_consent;
static bool s_rewrite_pending;
static void write_script(const char *name, const char *source);
static const tos_api_t s_api={.abi_major=1,.abi_minor=9};
const tos_api_t *tos_api_get(void) { return &s_api; }
esp_err_t tos_app_ctx_bind(void *task,tos_app_ctx_t *ctx) {
  assert(task==(void *)1 && ctx!=NULL);
  if(s_bind_should_fail) return ESP_ERR_NO_MEM;
  ++s_bindings; return ESP_OK;
}
void tos_app_ctx_unbind(void *task) { assert(task==(void *)1 && s_bindings==1); --s_bindings; }
void resource_release_owner_task(void *task) { assert(task==(void *)1 && s_bindings==1); ++s_releases; }
static void test_tick(void) {
  if(s_consent && s_snapshot.is_waiting_consent) {
    if(s_rewrite_pending) {
      write_script("caps.js","// @caps 0x08\nprint('changed after load');");
      s_rewrite_pending=false;
    }
    assert(script_runner_authorize(s_snapshot.generation+1,true)==ESP_ERR_INVALID_STATE);
    assert(script_runner_authorize(s_snapshot.generation,s_consent>0)==ESP_OK);
    s_consent=0;
  }
}

static void write_script(const char *name, const char *source) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", s_temporary_directory, name);
  FILE *file = fopen(path, "wb");
  assert(file != NULL);
  assert(fwrite(source, 1, strlen(source), file) == strlen(source));
  fclose(file);
}
static void finish(void) {
  assert(s_pending_task != NULL);
  void (*task)(void *) = s_pending_task;
  s_pending_task = NULL;
  task(NULL);
}
int main(int argc, char **argv) {
  assert(argc == 2);
  s_temporary_directory = argv[1];
  script_runner_snapshot_t snapshot;
  assert(script_runner_name_is_valid("hello.js"));
  assert(script_runner_name_is_valid("hello.lua"));
  assert(!script_runner_name_is_valid(NULL));
  assert(!script_runner_name_is_valid("../bad.js"));
  assert(!script_runner_name_is_valid(".hidden.lua"));
  assert(!script_runner_name_is_valid("sub/run.js"));
  assert(!script_runner_name_is_valid("fake.hb"));
  write_script("hello.js", "print('JavaScript from SD');");
  write_script("hello.lua", "print('Lua from SD')");
  assert(script_runner_start("../bad.js") == ESP_ERR_INVALID_ARG);
  s_task_should_fail = 1;
  assert(script_runner_start("hello.js") == ESP_ERR_NO_MEM);
  script_runner_snapshot(&snapshot);
  assert(!snapshot.is_running && snapshot.result == SCRIPT_ENGINE_NO_MEMORY);
  s_task_should_fail = 0;
  assert(script_runner_start("hello.js") == ESP_OK);
  assert(script_runner_start("hello.lua") == ESP_ERR_INVALID_STATE);
  finish();
  script_runner_snapshot(&snapshot);
  assert(!snapshot.is_running && snapshot.result == SCRIPT_ENGINE_OK);
  assert(strcmp(snapshot.output,"JavaScript from SD\n") == 0);
  assert(script_runner_start("hello.lua") == ESP_OK);
  finish();
  script_runner_snapshot(&snapshot);
  assert(snapshot.result == SCRIPT_ENGINE_OK && strcmp(snapshot.output,"Lua from SD\n") == 0);
  assert(script_runner_start("missing.js") == ESP_OK);
  finish();
  script_runner_snapshot(&snapshot);
  assert(snapshot.result == SCRIPT_ENGINE_ERROR && snapshot.error[0] != 0);
  assert(script_runner_start("hello.js") == ESP_OK);
  script_runner_stop();
  finish();
  script_runner_snapshot(&snapshot);
  assert(snapshot.result == SCRIPT_ENGINE_STOPPED && snapshot.output[0] == 0);
  write_script("loop.lua", "while true do end");
  assert(script_runner_start("loop.lua") == ESP_OK);
  finish();
  script_runner_snapshot(&snapshot);
  assert(snapshot.result == SCRIPT_ENGINE_STOPPED);
  write_script("output.js", "for(var i=0;i<2000;i++){print('0123456789');}");
  assert(script_runner_start("output.js") == ESP_OK);
  finish();
  script_runner_snapshot(&snapshot);
  assert(snapshot.result == SCRIPT_ENGINE_OK);
  assert(strlen(snapshot.output) == SCRIPT_RUNNER_OUTPUT_MAX - 1);
  assert(s_finished_tasks == 6 && s_bindings==0);
  s_bind_should_fail=true;
  assert(script_runner_start("hello.js")==ESP_OK); finish();
  script_runner_snapshot(&snapshot);
  assert(snapshot.result==SCRIPT_ENGINE_ERROR && snapshot.output[0]==0);
  s_bind_should_fail=false;
  write_script("caps.js","// @caps 0x08\nprint(tos.call('version').major);");
  s_consent=1; s_rewrite_pending=true;
  assert(script_runner_start("caps.js")==ESP_OK); finish();
  script_runner_snapshot(&snapshot);
  assert(snapshot.result==SCRIPT_ENGINE_OK && strcmp(snapshot.output,"1\n")==0);
  s_consent=-1;
  assert(script_runner_start("caps.js")==ESP_OK); finish();
  script_runner_snapshot(&snapshot);
  assert(snapshot.result==SCRIPT_ENGINE_STOPPED && snapshot.output[0]==0);
  assert(script_runner_start("caps.js")==ESP_OK); finish();
  script_runner_snapshot(&snapshot);
  assert(snapshot.result==SCRIPT_ENGINE_STOPPED && !snapshot.is_waiting_consent);
  assert(script_runner_authorize(snapshot.generation,true)==ESP_ERR_INVALID_STATE);
  write_script("caps.lua","-- @caps 0x02\nprint('never')");
  assert(script_runner_start("caps.lua")==ESP_OK); finish();
  script_runner_snapshot(&snapshot);
  assert(snapshot.result==SCRIPT_ENGINE_ERROR && snapshot.output[0]==0);
  assert(s_bindings==0);
  puts("PASS: SD JS/Lua execution, task failure, busy ownership, stop, deadline and bounded output");
}
