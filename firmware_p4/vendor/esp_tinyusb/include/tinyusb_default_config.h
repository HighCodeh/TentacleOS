/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include "tinyusb.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @cond INTERNAL */
#define GET_CONFIG_MACRO(dummy, arg1, arg2, arg3, name, ...)    name

#define TINYUSB_CONFIG_INVALID(...)              static_assert(false, "Too many arguments for TINYUSB_DEFAULT_CONFIG")
/** @endcond */

/**
 * @brief Initialize a TinyUSB driver configuration with target defaults.
 *
 * Supported invocations are:
 * - `TINYUSB_DEFAULT_CONFIG()`
 * - `TINYUSB_DEFAULT_CONFIG(event_cb)`
 * - `TINYUSB_DEFAULT_CONFIG(event_cb, event_arg)`
 *
 * The default port is `TINYUSB_PORT_HIGH_SPEED_0` on ESP32-P4 and ESP32-S31.
 * `TINYUSB_PORT_FULL_SPEED_0` on other supported targets. The default task
 * settings come from TINYUSB_TASK_DEFAULT().
 */
#define TINYUSB_DEFAULT_CONFIG(...)              GET_CONFIG_MACRO(, ##__VA_ARGS__, \
                                                                    TINYUSB_CONFIG_INVALID,    \
                                                                    TINYUSB_CONFIG_EVENT_ARG,  \
                                                                    TINYUSB_CONFIG_EVENT,      \
                                                                    TINYUSB_CONFIG_NO_ARG      \
                                                                )(__VA_ARGS__)

/** @cond INTERNAL */
// Helper: default high-speed port index. Only defined on HS-capable targets:
//  - Multi-port chips (e.g. ESP32-P4) have a dedicated HS port enum value.
//  - Single-port HS-only chips (e.g. ESP32-S31) use port 0, which is inherently HS.
// On FS-only targets (e.g. ESP32-S2/S3/H4) this macro is intentionally left
// undefined so that any use of TINYUSB_CONFIG_HIGH_SPEED fails to compile.
#if (SOC_USB_OTG_PERIPH_NUM > 1)
#define TINYUSB_PORT_DEFAULT_HS  TINYUSB_PORT_HIGH_SPEED_0
#elif CONFIG_IDF_TARGET_ESP32S31
#define TINYUSB_PORT_DEFAULT_HS  TINYUSB_PORT_FULL_SPEED_0
#endif

#if CONFIG_IDF_TARGET_ESP32P4 || CONFIG_IDF_TARGET_ESP32S31
#define TINYUSB_CONFIG_NO_ARG()                  TINYUSB_CONFIG_HIGH_SPEED(NULL, NULL)
#define TINYUSB_CONFIG_EVENT(event_hdl)          TINYUSB_CONFIG_HIGH_SPEED(event_hdl, NULL)
#define TINYUSB_CONFIG_EVENT_ARG(event_hdl, arg) TINYUSB_CONFIG_HIGH_SPEED(event_hdl, arg)
#else
#define TINYUSB_CONFIG_NO_ARG()                  TINYUSB_CONFIG_FULL_SPEED(NULL, NULL)
#define TINYUSB_CONFIG_EVENT(event_hdl)          TINYUSB_CONFIG_FULL_SPEED(event_hdl, NULL)
#define TINYUSB_CONFIG_EVENT_ARG(event_hdl, arg) TINYUSB_CONFIG_FULL_SPEED(event_hdl, arg)
#endif
/** @endcond */

/**
 * @brief Default TinyUSB task affinity.
 *
 * The default task runs on CPU0 in unicore builds and CPU1 in multicore
 * builds.
 */
#if CONFIG_FREERTOS_UNICORE
#define TINYUSB_DEFAULT_TASK_AFFINITY  (0U)
#else
#define TINYUSB_DEFAULT_TASK_AFFINITY  (1U)
#endif // CONFIG_FREERTOS_UNICORE

/**
 * @brief Default TinyUSB task stack size in bytes.
 */
#define TINYUSB_DEFAULT_TASK_SIZE      4096

/**
 * @brief Default TinyUSB task priority.
 */
#define TINYUSB_DEFAULT_TASK_PRIO      5

/**
 * @brief Initialize a full-speed TinyUSB driver configuration.
 *
 * The resulting initializer uses the default PHY settings, default task
 * settings from TINYUSB_TASK_DEFAULT(), and empty descriptor pointers so the
 * stack can fall back to built-in defaults when available.
 *
 * @param event_hdl Event callback assigned to tinyusb_config_t.event_cb.
 * @param arg User argument assigned to tinyusb_config_t.event_arg.
 */
#define TINYUSB_CONFIG_FULL_SPEED(event_hdl, arg)       \
    (tinyusb_config_t) {                                \
        .port = TINYUSB_PORT_FULL_SPEED_0,              \
        .phy = {                                        \
            .skip_setup = false,                        \
            .self_powered = false,                      \
            .vbus_monitor_io = -1,                      \
        },                                              \
        .task = TINYUSB_TASK_DEFAULT(),                 \
        .descriptor = {                                 \
            .device = NULL,                             \
            .qualifier = NULL,                          \
            .string = NULL,                             \
            .string_count = 0,                          \
            .full_speed_config = NULL,                  \
            .high_speed_config = NULL,                  \
        },                                              \
        .event_cb = (event_hdl),                        \
        .event_arg = (arg),                             \
    }

/**
 * @brief Initialize a high-speed TinyUSB driver configuration.
 *
 * The resulting initializer uses the default PHY settings, default task
 * settings from TINYUSB_TASK_DEFAULT(), and empty descriptor pointers so the
 * stack can fall back to built-in defaults when available.
 *
 * @note Only defined on high-speed-capable targets (e.g. ESP32-P4, ESP32-S31).
 *       Using this macro on a full-speed-only target results in a compile-time
 *       error; use TINYUSB_CONFIG_FULL_SPEED() there instead.
 *
 * @param event_hdl Event callback assigned to tinyusb_config_t.event_cb.
 * @param arg User argument assigned to tinyusb_config_t.event_arg.
 */
#if (SOC_USB_OTG_PERIPH_NUM > 1) || CONFIG_IDF_TARGET_ESP32S31
#define TINYUSB_CONFIG_HIGH_SPEED(event_hdl, arg)       \
    (tinyusb_config_t) {                                \
        .port = TINYUSB_PORT_DEFAULT_HS,                \
        .phy = {                                        \
            .skip_setup = false,                        \
            .self_powered = false,                      \
            .vbus_monitor_io = -1,                      \
        },                                              \
        .task = TINYUSB_TASK_DEFAULT(),                 \
        .descriptor = {                                 \
            .device = NULL,                             \
            .qualifier = NULL,                          \
            .string = NULL,                             \
            .string_count = 0,                          \
            .full_speed_config = NULL,                  \
            .high_speed_config = NULL,                  \
        },                                              \
        .event_cb = (event_hdl),                        \
        .event_arg = (arg),                             \
    }
#endif // HS-capable target

/**
 * @brief Initialize a TinyUSB task configuration with default values.
 */
#define TINYUSB_TASK_DEFAULT()                          \
    (tinyusb_task_config_t) {                           \
        .size = TINYUSB_DEFAULT_TASK_SIZE,              \
        .priority = TINYUSB_DEFAULT_TASK_PRIO,          \
        .xCoreID = TINYUSB_DEFAULT_TASK_AFFINITY,       \
    }

/**
 * @brief Initialize a custom TinyUSB task configuration.
 *
 * @param s Task stack size in bytes.
 * @param p Task priority.
 * @param a Task affinity.
 */
#define TINYUSB_TASK_CUSTOM(s, p, a)                    \
    (tinyusb_task_config_t) {                           \
        .size = (s),                                    \
        .priority = (p),                                \
        .xCoreID = (a),                                 \
    }

#ifdef __cplusplus
}
#endif
