// Copyright (c) 2026 HIGH CODE LLC
// SPDX-License-Identifier: GPL-3.0-or-later
#include "script_engine.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>

#include "esp_log.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "mquickjs.h"

#define LUA_HOOK_INSTRUCTIONS 1000
#define SCRIPT_SLEEP_MAX_MS   30000
#define SCRIPT_MS_PER_SECOND  1000

static const char *TAG = "SCRIPT_ENGINE";

typedef struct {
  script_engine_request_t *request;
  size_t allocated;
  bool is_stopped;
} script_engine_state_t;

typedef union {
  max_align_t alignment;
  size_t length;
} script_engine_allocation_t;

static bool poll(script_engine_state_t *state) {
  if (state->request->host->poll(state->request->host->ctx))
    state->is_stopped = true;
  return state->is_stopped;
}

static void emit(script_engine_state_t *state, const char *text, size_t length) {
  state->request->host->output(state->request->host->ctx, text, length);
}

static void *allocate(script_engine_state_t *state, size_t length) {
  void *memory = state->request->host->alloc(length);
  if (memory == NULL)
    ESP_LOGE(TAG, "Interpreter allocation failed (%zu bytes)", length);
  return memory;
}

static void *lua_allocate(void *ctx, void *pointer, size_t old_size, size_t new_size) {
  (void)old_size;
  script_engine_state_t *state = ctx;
  script_engine_allocation_t *old =
      pointer == NULL ? NULL : (script_engine_allocation_t *)pointer - 1;
  size_t old_length = old == NULL ? 0 : old->length;
  if (new_size == 0) {
    state->allocated -= old == NULL ? 0 : old_length + sizeof(*old);
    free(old);
    return NULL;
  }
  size_t old_cost = old == NULL ? 0 : old_length + sizeof(*old);
  if (new_size > SCRIPT_ENGINE_HEAP_MAX - sizeof(*old))
    return NULL;
  size_t new_cost = sizeof(*old) + new_size;
  if (new_cost > SCRIPT_ENGINE_HEAP_MAX - state->allocated)
    return NULL;
  script_engine_allocation_t *memory = allocate(state, new_cost);
  if (memory == NULL)
    return NULL;
  memory->length = new_size;
  if (old != NULL)
    memcpy(memory + 1, pointer, old_length < new_size ? old_length : new_size);
  state->allocated = state->allocated - old_cost + new_cost;
  free(old);
  return memory + 1;
}

static script_engine_state_t *lua_state(lua_State *lua) {
  return *(script_engine_state_t **)lua_getextraspace(lua);
}

static void lua_poll(lua_State *lua, lua_Debug *debug) {
  (void)debug;
  if (poll(lua_state(lua))) {
    lua_sethook(lua, lua_poll, LUA_MASKCOUNT, 1);
    luaL_error(lua, "Script stopped");
  }
}

static int lua_print(lua_State *lua) {
  script_engine_state_t *state = lua_state(lua);
  int count = lua_gettop(lua);
  for (int i = 1; i <= count; ++i) {
    size_t length;
    const char *text = luaL_tolstring(lua, i, &length);
    if (i > 1)
      emit(state, "\t", 1);
    emit(state, text, length);
    lua_pop(lua, 1);
  }
  emit(state, "\n", 1);
  return 0;
}

static int lua_sleep(lua_State *lua) {
  lua_Integer milliseconds = luaL_checkinteger(lua, 1);
  if (milliseconds < 0 || milliseconds > SCRIPT_SLEEP_MAX_MS)
    return luaL_error(lua, "sleep expects 0..30000 milliseconds");
  script_engine_state_t *state = lua_state(lua);
  state->request->host->sleep(state->request->host->ctx, (uint32_t)milliseconds);
  if (poll(state))
    return luaL_error(lua, "Script stopped");
  return 0;
}

static int lua_millis(lua_State *lua) {
  script_engine_state_t *state = lua_state(lua);
  lua_pushinteger(lua, state->request->host->clock(state->request->host->ctx));
  return 1;
}

static int lua_bootstrap(lua_State *lua) {
  static const luaL_Reg LIBRARIES[] = {{LUA_GNAME, luaopen_base},
                                       {LUA_TABLIBNAME, luaopen_table},
                                       {LUA_STRLIBNAME, luaopen_string},
                                       {LUA_MATHLIBNAME, luaopen_math},
                                       {LUA_UTF8LIBNAME, luaopen_utf8},
                                       {NULL, NULL}};
  for (const luaL_Reg *library = LIBRARIES; library->func != NULL; ++library) {
    luaL_requiref(lua, library->name, library->func, 1);
    lua_pop(lua, 1);
  }
  const char *RESTRICTED[] = {"dofile", "loadfile", "load", "collectgarbage"};
  for (size_t i = 0; i < sizeof(RESTRICTED) / sizeof(RESTRICTED[0]); ++i) {
    lua_pushnil(lua);
    lua_setglobal(lua, RESTRICTED[i]);
  }
  lua_register(lua, "print", lua_print);
  lua_register(lua, "sleep", lua_sleep);
  lua_register(lua, "millis", lua_millis);
  return 0;
}

static script_engine_result_t run_lua(script_engine_state_t *state) {
  lua_State *lua = lua_newstate(lua_allocate, state);
  if (lua == NULL)
    return SCRIPT_ENGINE_NO_MEMORY;
  *(script_engine_state_t **)lua_getextraspace(lua) = state;
  lua_sethook(lua, lua_poll, LUA_MASKCOUNT, LUA_HOOK_INSTRUCTIONS);
  lua_pushcfunction(lua, lua_bootstrap);
  int result = lua_pcall(lua, 0, 0, 0);
  if (result == LUA_OK)
    result = luaL_loadbufferx(
        lua, state->request->source, state->request->length, state->request->name, "t");
  if (result == LUA_OK)
    result = lua_pcall(lua, 0, 0, 0);
  if (result != LUA_OK) {
    const char *error = lua_tostring(lua, -1);
    snprintf(state->request->error,
             sizeof(state->request->error),
             "%s",
             error == NULL ? "Lua execution failed" : error);
  }
  lua_close(lua);
  if (state->is_stopped)
    return SCRIPT_ENGINE_STOPPED;
  return result == LUA_OK       ? SCRIPT_ENGINE_OK
         : result == LUA_ERRMEM ? SCRIPT_ENGINE_NO_MEMORY
                                : SCRIPT_ENGINE_ERROR;
}

static int js_interrupt(JSContext *js, void *ctx) {
  (void)js;
  return poll(ctx);
}

static void js_output(void *ctx, const void *text, size_t length) {
  emit(ctx, text, length);
}

static JSValue js_print(JSContext *js, JSValue *this_value, int count, JSValue *arguments) {
  (void)this_value;
  script_engine_state_t *state = JS_GetContextOpaque(js);
  for (int i = 0; i < count; ++i) {
    JSCStringBuf buffer;
    size_t length;
    const char *text = JS_ToCStringLen(js, &length, arguments[i], &buffer);
    if (text == NULL)
      return JS_EXCEPTION;
    if (i > 0)
      emit(state, " ", 1);
    emit(state, text, length);
  }
  emit(state, "\n", 1);
  return JS_UNDEFINED;
}

static JSValue js_sleep(JSContext *js, JSValue *this_value, int count, JSValue *arguments) {
  (void)this_value;
  (void)count;
  double milliseconds;
  if (JS_ToNumber(js, &milliseconds, arguments[0]))
    return JS_EXCEPTION;
  if (!isfinite(milliseconds) || milliseconds < 0 || milliseconds > SCRIPT_SLEEP_MAX_MS ||
      trunc(milliseconds) != milliseconds)
    return JS_ThrowRangeError(js, "sleep expects 0..30000 milliseconds");
  script_engine_state_t *state = JS_GetContextOpaque(js);
  state->request->host->sleep(state->request->host->ctx, (uint32_t)milliseconds);
  if (poll(state))
    return JS_ThrowInternalError(js, "Script stopped");
  return JS_UNDEFINED;
}

static JSValue
js_performance_now(JSContext *js, JSValue *this_value, int count, JSValue *arguments) {
  (void)this_value;
  (void)count;
  (void)arguments;
  script_engine_state_t *state = JS_GetContextOpaque(js);
  return JS_NewInt64(js, state->request->host->clock(state->request->host->ctx));
}

static JSValue js_date_now(JSContext *js, JSValue *this_value, int count, JSValue *arguments) {
  (void)this_value;
  (void)count;
  (void)arguments;
  struct timeval now;
  gettimeofday(&now, NULL);
  return JS_NewInt64(
      js, (int64_t)now.tv_sec * SCRIPT_MS_PER_SECOND + now.tv_usec / SCRIPT_MS_PER_SECOND);
}

static JSValue
js_date_constructor(JSContext *js, JSValue *this_value, int count, JSValue *arguments) {
  (void)this_value;
  double milliseconds;
  if ((count & ~FRAME_CF_CTOR) == 0) {
    struct timeval now;
    gettimeofday(&now, NULL);
    milliseconds = (double)now.tv_sec * SCRIPT_MS_PER_SECOND + now.tv_usec / SCRIPT_MS_PER_SECOND;
  } else if (JS_ToNumber(js, &milliseconds, arguments[0])) {
    return JS_EXCEPTION;
  }
  return JS_NewDate(js, milliseconds);
}

#if UINTPTR_MAX == UINT32_MAX
#include "script_js_rom_32.h"
#else
#include "script_js_rom_64.h"
#endif

static script_engine_result_t run_js(script_engine_state_t *state) {
  void *memory = allocate(state, SCRIPT_ENGINE_HEAP_MAX);
  if (memory == NULL)
    return SCRIPT_ENGINE_NO_MEMORY;
  JSContext *js = JS_NewContext(memory, SCRIPT_ENGINE_HEAP_MAX, &js_stdlib);
  if (js == NULL) {
    free(memory);
    return SCRIPT_ENGINE_NO_MEMORY;
  }
  JS_SetContextOpaque(js, state);
  JS_SetInterruptHandler(js, js_interrupt);
  JS_SetLogFunc(js, js_output);
  JSValue value =
      JS_Eval(js, state->request->source, state->request->length, state->request->name, 0);
  bool has_error = JS_IsException(value);
  if (has_error) {
    JSCStringBuf buffer;
    const char *error = JS_ToCString(js, JS_GetException(js), &buffer);
    snprintf(state->request->error,
             sizeof(state->request->error),
             "%s",
             error == NULL ? "JavaScript execution failed" : error);
  }
  JS_FreeContext(js);
  free(memory);
  return state->is_stopped ? SCRIPT_ENGINE_STOPPED
         : has_error       ? SCRIPT_ENGINE_ERROR
                           : SCRIPT_ENGINE_OK;
}

script_engine_result_t script_engine_run(script_engine_request_t *request) {
  if (request == NULL || request->source == NULL || request->name == NULL ||
      request->host == NULL || request->length == 0 || request->length > SCRIPT_ENGINE_SOURCE_MAX ||
      memchr(request->source, 0, request->length) != NULL || request->language < 0 ||
      request->language >= SCRIPT_ENGINE_LANGUAGE_COUNT || request->host->alloc == NULL ||
      request->host->poll == NULL || request->host->output == NULL ||
      request->host->sleep == NULL || request->host->clock == NULL)
    return SCRIPT_ENGINE_INVALID_ARG;
  request->error[0] = '\0';
  script_engine_state_t state = {.request = request};
  ESP_LOGD(TAG, "Running %s", request->name);
  if (poll(&state))
    return SCRIPT_ENGINE_STOPPED;
  return request->language == SCRIPT_ENGINE_JS ? run_js(&state) : run_lua(&state);
}
