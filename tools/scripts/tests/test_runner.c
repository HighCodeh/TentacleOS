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
  assert(s_finished_tasks == 6);
  puts("PASS: SD JS/Lua execution, task failure, busy ownership, stop, deadline and bounded output");
}
