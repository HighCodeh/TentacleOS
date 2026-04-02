# TentacleOS — Coding Standards

This guide can't cover everything, but it covers the rules we enforce on PR reviews.
Formatting is handled automatically by `.clang-format` — run it before committing.
3rd party libraries and managed components are not our concern.

This set is not final. If you want to change something, open a ticket.

## Inspiration

- [ESP-IDF Style Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/contribute/style-guide.html)
- [Linux Kernel Coding Style](https://www.kernel.org/doc/html/v4.10/process/coding-style.html)
- [Flipper Zero Coding Style](https://github.com/flipperdevices/flipperzero-firmware/blob/dev/CODING_STYLE.md)


## General Rules

- Code is public. Keep it simple, keep it readable.
- If you need a comment to explain a line, the line is too clever.
- Leave references to datasheets or standards you are implementing.
- Every literal with domain meaning must be a named `#define`. Exceptions: `0`, `1`, `NULL`, `true`, `false`.


## Naming

### Functions

Public functions use the module name as prefix. Static functions drop it.

    subghz_receiver_start()    // public
    cc1101_set_frequency()     // public
    process_pulse()            // static
    build_histogram()          // static

### Variables

| Scope | Rule | Example |
|-------|------|---------|
| Local | `snake_case` | `pulse_duration_us` |
| Static | `s_` prefix | `s_is_running` |
| Global (avoid) | `g_` prefix | `g_system_state` |
| Boolean | `is_`, `has_`, `can_` | `is_running`, `has_data` |
| Output parameter | `out_` prefix | `out_result`, `out_buffer` |

Names must say what they hold. Single-letter variables only as loop counters in short loops.

### Types

All types use `snake_case` with `_t` suffix, prefixed by the module name.

    subghz_data_t
    cc1101_preset_t
    subghz_spectrum_line_t

Callback typedefs use `_cb_t` suffix.

    subghz_rx_done_cb_t

### Enums

Values are `UPPER_SNAKE_CASE` with module prefix. Include a `_COUNT` sentinel when iterable.

```c
typedef enum {
  SUBGHZ_MODE_SCAN = 0,
  SUBGHZ_MODE_RAW,
  SUBGHZ_MODE_COUNT
} subghz_mode_t;
```

### Constants and Macros

`UPPER_SNAKE_CASE`. GPIO pins follow the pattern `GPIO_NAME_PIN`.

    #define RMT_RESOLUTION_HZ   1000000
    #define GPIO_CS_PIN          3
    #define GPIO_SCL_PIN         9

### Static Const Arrays

`UPPER_SNAKE_CASE` with a `_COUNT` companion macro.

```c
static const uint32_t HOP_FREQUENCIES[] = {
  433920000,
  868350000,
  315000000,
};
#define HOP_FREQUENCIES_COUNT (sizeof(HOP_FREQUENCIES) / sizeof(HOP_FREQUENCIES[0]))
```

### Files

- Directories: `^[0-9a-z_]+$`
- Source files: `^[0-9a-z_]+\.[ch]$`
- File name is a prefix for its content: `subghz_receiver.c` → `subghz_receiver_start()`

### Standard Function Suffixes

| Suffix | Meaning |
|--------|---------|
| `_init` | Initialize module/peripheral. Does not allocate. |
| `_alloc` | Allocate and init an instance. Returns pointer. |
| `_free` | Deinit and release an instance. |
| `_start` | Begin an async operation or task. |
| `_stop` | Request graceful shutdown. |
| `_get` | Read a value. No side effects. |
| `_set` | Write a value. |


## Types

Use `<stdint.h>` fixed-width types for hardware-related code.

| Use | Type |
|-----|------|
| Register values, byte buffers | `uint8_t` |
| Frequencies, addresses | `uint32_t` |
| Signed pulse timings | `int32_t` |
| Buffer sizes, array indices | `size_t` |
| GPIO pins | `gpio_num_t` |
| Booleans | `bool` |
| Error codes | `esp_err_t` |

Never rely on implicit `int`. Always be explicit about types.

All structs use `typedef` with `_t` suffix. Internal structs (file-scope) follow the same rule, prefixed with the module.


## Functions

- Each function does **one thing**. A long function that does one thing is fine. A short function that does three things is not.
- Maximum **4 parameters**. Group into a config struct if you need more.
- If two functions share more than 70% of their body, unify them with a parameter.


## Error Handling

- Public functions return `esp_err_t` when they can fail. Use `void` only when failure is impossible.
- Every `malloc` / `heap_caps_malloc` / `heap_caps_aligned_alloc` must be checked for `NULL`, logged with `ESP_LOGE`, and handled (return error or `goto cleanup`).
- Always use `free()` to deallocate memory, regardless of which allocation function was used (`malloc`, `heap_caps_malloc`, `heap_caps_aligned_alloc`). The `heap_caps_aligned_free` is deprecated since ESP-IDF v5.5.

- Use `goto cleanup` for functions with multiple resources:

```c
esp_err_t do_work(void) {
  esp_err_t ret = ESP_OK;
  uint8_t *buf_a = NULL;
  uint8_t *buf_b = NULL;

  buf_a = malloc(BUF_A_SIZE);
  if (buf_a == NULL) {
    ESP_LOGE(TAG, "Failed to allocate buf_a");
    ret = ESP_ERR_NO_MEM;
    goto cleanup;
  }

  buf_b = malloc(BUF_B_SIZE);
  if (buf_b == NULL) {
    ESP_LOGE(TAG, "Failed to allocate buf_b");
    ret = ESP_ERR_NO_MEM;
    goto cleanup;
  }

  // work

cleanup:
  free(buf_b);
  free(buf_a);
  return ret;
}
```


## Safety

- Always validate array indices before access. `array[i + 1]` requires `i < size - 1`.
- Never use `strcat` in a loop. Use `snprintf` with offset tracking.
- Use `break` to exit loops. Never assign the loop counter to force exit.
- Use explicit `== NULL` for pointer checks. `!value` is only for booleans.


## Concurrency

- Any variable accessed by more than one task must be protected by a mutex or atomic. `volatile` alone is not thread-safe.
- Document buffer ownership: who allocates, who frees, when.
- Never free a buffer while a peripheral (RMT, SPI, DMA) may still be using it.
- All shared state must be initialized before `xTaskCreate`.
- Tasks must release all resources before `vTaskDelete`. Set handles to `NULL` after deletion.


## Headers

- All headers use `#ifndef` include guards.
- All headers wrap declarations in `extern "C"` for C++ compatibility.
- Include order, separated by blank lines:
  1. Corresponding header
  2. C standard library
  3. ESP-IDF / FreeRTOS
  4. Project headers


## Logging

- Every `.c` file defines `static const char *TAG = "MODULE_NAME";` at the top.
- All debug output goes through `ESP_LOGx`. Never use `printf`.

| Level | When |
|-------|------|
| `ESP_LOGE` | Errors that break functionality |
| `ESP_LOGW` | Recoverable issues |
| `ESP_LOGI` | Lifecycle events (init, start, stop) |
| `ESP_LOGD` | Debug details |


## File Organization

### Component structure

```
components/
  module_name/
    include/
      module_name.h
      module_name_types.h
    module_name.c
    CMakeLists.txt
```

### Source file layout

```
1. License header
2. Corresponding header include
3. C standard library includes
4. ESP-IDF / FreeRTOS includes
5. Project includes
6. #define constants
7. Static types
8. Static variables
9. Static function forward declarations
10. Public function implementations
11. Static function implementations
```


## Reference — Header File

```c
// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// ...

#ifndef MODULE_NAME_H
#define MODULE_NAME_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "esp_err.h"

#define MODULE_NAME_MAX_ITEMS  64
#define MODULE_NAME_TIMEOUT_MS 5000

/**
 * @brief Operating modes for the module.
 */
typedef enum {
  MODULE_NAME_MODE_IDLE = 0,
  MODULE_NAME_MODE_ACTIVE,
  MODULE_NAME_MODE_COUNT
} module_name_mode_t;

/**
 * @brief Configuration for module initialization.
 */
typedef struct {
  uint32_t            frequency;
  uint8_t             channel;
  module_name_mode_t  mode;
  bool                is_looping;
} module_name_config_t;

/**
 * @brief Result data returned after processing.
 */
typedef struct {
  uint32_t    value;
  uint8_t     status;
  const char *label;
} module_name_result_t;

/**
 * @brief Callback invoked when processing is complete.
 *
 * @param result  Pointer to the result data. Valid only during callback scope.
 * @param ctx     User context passed during registration.
 */
typedef void (*module_name_done_cb_t)(const module_name_result_t *result, void *ctx);

/**
 * @brief Initialize the module.
 *
 * @param config  Pointer to configuration. Caller retains ownership.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_ARG if config is NULL
 *   - ESP_ERR_NO_MEM if allocation fails
 */
esp_err_t module_name_init(const module_name_config_t *config);

/**
 * @brief Start the module processing task.
 *
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_STATE if already running
 */
esp_err_t module_name_start(void);

/**
 * @brief Stop the module and release resources.
 */
void module_name_stop(void);

/**
 * @brief Check if the module is currently running.
 *
 * @return true if running, false otherwise.
 */
bool module_name_is_running(void);

/**
 * @brief Get the latest result.
 *
 * @param[out] out_result  Pointer to store the result. Must not be NULL.
 * @return
 *   - ESP_OK on success
 *   - ESP_ERR_INVALID_STATE if no result available
 */
esp_err_t module_name_get_result(module_name_result_t *out_result);

/**
 * @brief Set a value in the module.
 *
 * @param value  The value to set.
 */
void module_name_set_value(uint32_t value);

/**
 * @brief Allocate a new module instance.
 *
 * @return Pointer to instance, or NULL on failure.
 */
module_name_result_t *module_name_result_alloc(void);

/**
 * @brief Free a previously allocated instance.
 *
 * @param result  Pointer to instance. NULL is safe.
 */
void module_name_result_free(module_name_result_t *result);

#ifdef __cplusplus
}
#endif

#endif // MODULE_NAME_H
```


## Reference — Source File

```c
// Copyright (c) 2025 HIGH CODE LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// ...

#include "module_name.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "other_module.h"

static const char *TAG = "MODULE_NAME";

#define TASK_STACK_SIZE   4096
#define TASK_PRIORITY     5
#define TASK_CORE         1
#define PROCESS_DELAY_MS  100

typedef struct {
  int32_t *items;
  size_t   count;
} module_name_buffer_t;

static TaskHandle_t s_task_handle = NULL;
static SemaphoreHandle_t s_mutex = NULL;
static volatile bool s_is_running = false;
static module_name_config_t s_config = {0};
static module_name_result_t s_result = {0};

static void process_task(void *pvParameters);
static esp_err_t process_data(const int32_t *data, size_t count, module_name_result_t *out_result);
static void reset_state(void);

esp_err_t module_name_init(const module_name_config_t *config) {
  if (config == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  s_config = *config;

  if (s_mutex == NULL) {
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
      ESP_LOGE(TAG, "Failed to create mutex");
      return ESP_ERR_NO_MEM;
    }
  }

  ESP_LOGI(TAG, "Initialized — mode: %d, freq: %lu",
      (int)config->mode, (unsigned long)config->frequency);

  return ESP_OK;
}

esp_err_t module_name_start(void) {
  if (s_is_running) {
    return ESP_ERR_INVALID_STATE;
  }

  BaseType_t ret = xTaskCreatePinnedToCore(
      process_task, "module_name", TASK_STACK_SIZE, NULL,
      TASK_PRIORITY, &s_task_handle, TASK_CORE);

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create task");
    return ESP_ERR_NO_MEM;
  }

  return ESP_OK;
}

void module_name_stop(void) {
  s_is_running = false;
}

bool module_name_is_running(void) {
  return s_is_running;
}

esp_err_t module_name_get_result(module_name_result_t *out_result) {
  if (out_result == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(PROCESS_DELAY_MS)) != pdTRUE) {
    return ESP_ERR_TIMEOUT;
  }

  *out_result = s_result;
  xSemaphoreGive(s_mutex);

  return ESP_OK;
}

void module_name_set_value(uint32_t value) {
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(PROCESS_DELAY_MS)) == pdTRUE) {
    s_result.value = value;
    xSemaphoreGive(s_mutex);
  }
}

module_name_result_t *module_name_result_alloc(void) {
  module_name_result_t *result = malloc(sizeof(module_name_result_t));
  if (result == NULL) {
    ESP_LOGE(TAG, "Failed to allocate result");
    return NULL;
  }

  memset(result, 0, sizeof(module_name_result_t));
  return result;
}

void module_name_result_free(module_name_result_t *result) {
  free(result);
}

static void process_task(void *pvParameters) {
  s_is_running = true;
  ESP_LOGI(TAG, "Task started");

  int32_t *buffer = malloc(MODULE_NAME_MAX_ITEMS * sizeof(int32_t));
  if (buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate buffer");
    goto cleanup;
  }

  while (s_is_running) {
    // Simulate filling buffer
    size_t count = MODULE_NAME_MAX_ITEMS;

    module_name_result_t result = {0};
    if (process_data(buffer, count, &result) == ESP_OK) {
      if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(PROCESS_DELAY_MS)) == pdTRUE) {
        s_result = result;
        xSemaphoreGive(s_mutex);
      }
    }

    vTaskDelay(pdMS_TO_TICKS(PROCESS_DELAY_MS));
  }

cleanup:
  free(buffer);
  reset_state();
  vTaskDelete(NULL);
}

static esp_err_t process_data(const int32_t *data, size_t count, module_name_result_t *out_result) {
  if (data == NULL || count == 0 || out_result == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  uint32_t sum = 0;
  for (size_t i = 0; i < count; i++) {
    sum += (uint32_t)abs(data[i]);
  }

  out_result->value = sum / count;
  out_result->status = 1;
  out_result->label = "processed";

  ESP_LOGD(TAG, "Processed %d items, avg: %lu", (int)count, (unsigned long)out_result->value);

  return ESP_OK;
}

static void reset_state(void) {
  s_task_handle = NULL;
  s_is_running = false;
  ESP_LOGI(TAG, "Task stopped");
}
```
