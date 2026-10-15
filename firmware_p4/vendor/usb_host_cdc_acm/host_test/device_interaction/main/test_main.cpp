/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <catch2/catch_session.hpp>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

struct MainTaskArgs {
    int argc;
    const char **argv;
};

static void main_task(void *args)
{
    MainTaskArgs *task_args = (MainTaskArgs *)args;
    auto result = Catch::Session().run(task_args->argc, task_args->argv);

    fflush(stdout);
    delete task_args;
    exit(result);
    vTaskDelete(NULL);
}

extern "C" int __wrap_main(int argc, const char **argv)
{
    // Following section is copied from components\freertos\FreeRTOS-Kernel\portable\linux\port_idf.c
    // It starts the FreeRTOS scheduler and creates the main task to run Catch2 tests.
    // Only difference from esp-idf implementation is passing of argc and argv to the main task.

    // This makes sure that stdio is always synchronized so that idf.py monitor
    // and other tools read text output on time.
    setvbuf(stdout, NULL, _IONBF, 0);

    usleep(1000);
    MainTaskArgs *task_args = new MainTaskArgs{argc, argv};
    BaseType_t res = xTaskCreatePinnedToCore(&main_task, "main",
                                             ESP_TASK_MAIN_STACK, task_args,
                                             ESP_TASK_MAIN_PRIO, NULL, ESP_TASK_MAIN_CORE);
    assert(res == pdTRUE);
    (void)res;

    vTaskStartScheduler();

    // This line should never be reached
    assert(false);
}
