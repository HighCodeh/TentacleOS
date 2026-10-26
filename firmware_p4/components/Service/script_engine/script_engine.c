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
#include "script_bindings.h"

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
  // Lua requires shrinking an allocation to succeed even when memory is exhausted.
  if (old != NULL && new_size <= old_length)
    return pointer;
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

static bool read_lua_arg(lua_State *lua, int index, script_bindings_value_t *out_value) {
  switch (lua_type(lua, index)) {
    case LUA_TNIL:
      out_value->type = '0';
      return true;
    case LUA_TBOOLEAN:
      out_value->type = 'b';
      out_value->number = lua_toboolean(lua, index);
      return true;
    case LUA_TNUMBER:
      out_value->type = 'n';
      out_value->number = lua_tonumber(lua, index);
      return true;
    case LUA_TSTRING: {
      size_t length;
      const char *text = lua_tolstring(lua, index, &length);
      if (length >= sizeof(out_value->text) || memchr(text, 0, length) != NULL)
        return false;
      out_value->type = 's';
      memcpy(out_value->text, text, length);
      out_value->text[length] = 0;
      return true;
    }
    default:
      return false;
  }
}

static void push_lua_json(lua_State *lua, const cJSON *value) {
  if (!lua_checkstack(lua, SCRIPT_BINDINGS_ARGS_MAX))
    luaL_error(lua, "ABI result stack exhausted");
  if (cJSON_IsNumber(value)) {
    if (value->valuedouble >= INT32_MIN && value->valuedouble <= UINT32_MAX &&
        trunc(value->valuedouble) == value->valuedouble)
      lua_pushinteger(lua, (lua_Integer)value->valuedouble);
    else
      lua_pushnumber(lua, value->valuedouble);
  } else if (cJSON_IsBool(value))
    lua_pushboolean(lua, cJSON_IsTrue(value));
  else if (cJSON_IsString(value))
    lua_pushstring(lua, value->valuestring);
  else if (cJSON_IsNull(value))
    lua_pushnil(lua);
  else {
    lua_newtable(lua);
    int index = 1;
    for (const cJSON *child = value->child; child != NULL; child = child->next) {
      push_lua_json(lua, child);
      if (cJSON_IsArray(value))
        lua_rawseti(lua, -2, index++);
      else
        lua_setfield(lua, -2, child->string);
    }
  }
}

static int lua_json_result(lua_State *lua) {
  push_lua_json(lua, lua_touserdata(lua, 1));
  return 1;
}

static int lua_abi_call(lua_State *lua) {
  size_t length;
  if (lua_type(lua, 1) != LUA_TSTRING)
    return luaL_error(lua, "tos.call expects a method name");
  const char *method = lua_tolstring(lua, 1, &length);
  if (memchr(method, 0, length) != NULL)
    return luaL_error(lua, "Invalid ABI method");
  int count = lua_gettop(lua) - 1;
  if (count > SCRIPT_BINDINGS_ARGS_MAX)
    return luaL_error(lua, "Too many ABI arguments");
  script_bindings_call_t call = {.method = method, .count = (uint32_t)count};
  for (int i = 0; i < count; ++i)
    if (!read_lua_arg(lua, i + 2, &call.args[i]))
      return luaL_error(lua, "Invalid ABI argument");
  // Reserve the protected result conversion frame before acquiring any resources.
  luaL_checkstack(lua, SCRIPT_BINDINGS_ARGS_MAX, "ABI call");
  script_engine_state_t *state = lua_state(lua);
  if (poll(state))
    return luaL_error(lua, "Script stopped");
  esp_err_t err = script_bindings_call(state->request->host->bindings, &call);
  if (err != ESP_OK) {
    cJSON_Delete(call.result);
    return luaL_error(lua, "ABI call failed: %d", err);
  }
  lua_pushcfunction(lua, lua_json_result);
  lua_pushlightuserdata(lua, call.result);
  int result = lua_pcall(lua, 1, 1, 0);
  cJSON_Delete(call.result);
  if (result != LUA_OK)
    return lua_error(lua);
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
  lua_newtable(lua);
  lua_pushcfunction(lua, lua_abi_call);
  lua_setfield(lua, -2, "call");
  lua_setglobal(lua, "tos");
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

static JSValue push_js_json(JSContext *js, const cJSON *value) {
  if (cJSON_IsNumber(value))
    return JS_NewFloat64(js, value->valuedouble);
  if (cJSON_IsBool(value))
    return JS_NewBool(cJSON_IsTrue(value));
  if (cJSON_IsString(value))
    return JS_NewString(js, value->valuestring);
  if (cJSON_IsNull(value))
    return JS_NULL;
  JSValue object = cJSON_IsArray(value) ? JS_NewArray(js, 0) : JS_NewObject(js);
  if (JS_IsException(object))
    return object;
  JSGCRef reference;
  JSValue *root = JS_PushGCRef(js, &reference);
  *root = object;
  uint32_t index = 0;
  for (const cJSON *child = value->child; child != NULL; child = child->next) {
    JSValue item = push_js_json(js, child);
    if (JS_IsException(item)) {
      JS_PopGCRef(js, &reference);
      return JS_EXCEPTION;
    }
    JSValue result = cJSON_IsArray(value) ? JS_SetPropertyUint32(js, *root, index++, item)
                                          : JS_SetPropertyStr(js, *root, child->string, item);
    if (JS_IsException(result)) {
      JS_PopGCRef(js, &reference);
      return JS_EXCEPTION;
    }
  }
  return JS_PopGCRef(js, &reference);
}

static JSValue js_abi_call(JSContext *js, JSValue *this_value, int count, JSValue *arguments) {
  (void)this_value;
  if (count == 0 || count > SCRIPT_BINDINGS_ARGS_MAX + 1 || !JS_IsString(js, arguments[0]))
    return JS_ThrowTypeError(js, "tos.call expects method and scalar arguments");
  script_bindings_call_t call = {.count = (uint32_t)count - 1};
  char method[SCRIPT_BINDINGS_STRING_MAX];
  for (int i = 0; i < count; ++i) {
    JSValue value = arguments[i];
    script_bindings_value_t *arg = i == 0 ? NULL : &call.args[i - 1];
    if (JS_IsString(js, value)) {
      JSCStringBuf buffer;
      size_t length;
      const char *text = JS_ToCStringLen(js, &length, value, &buffer);
      if (text == NULL)
        return JS_EXCEPTION;
      if (length >= SCRIPT_BINDINGS_STRING_MAX || memchr(text, 0, length) != NULL)
        return JS_ThrowTypeError(js, "ABI string too long or contains NUL");
      char *target = i == 0 ? method : arg->text;
      memcpy(target, text, length);
      target[length] = 0;
      if (arg != NULL)
        arg->type = 's';
    } else if (arg != NULL && JS_IsNumber(js, value)) {
      arg->type = 'n';
      if (JS_ToNumber(js, &arg->number, value))
        return JS_EXCEPTION;
    } else if (arg != NULL && JS_IsBool(value)) {
      arg->type = 'b';
      arg->number = (value == JS_TRUE);
    } else if (arg != NULL && JS_IsNull(value))
      arg->type = '0';
    else
      return JS_ThrowTypeError(js, "Invalid ABI argument");
  }
  call.method = method;
  script_engine_state_t *state = JS_GetContextOpaque(js);
  if (poll(state))
    return JS_ThrowInternalError(js, "Script stopped");
  esp_err_t err = script_bindings_call(state->request->host->bindings, &call);
  JSValue result = err == ESP_OK ? push_js_json(js, call.result)
                                 : JS_ThrowInternalError(js, "ABI call failed: %d", err);
  cJSON_Delete(call.result);
  return result;
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
