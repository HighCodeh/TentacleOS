/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>
#include "mdns_mem_caps.h"

void *mdns_mem_malloc(size_t size)
{
    return malloc(size);
}

void *mdns_mem_calloc(size_t num, size_t size)
{
    return calloc(num, size);
}

void mdns_mem_free(void *ptr)
{
    free(ptr);
}

char *mdns_mem_strdup(const char *s)
{
    if (s == NULL) {
        return NULL;
    }

    size_t len = strlen(s) + 1;
    char *new_str = mdns_mem_malloc(len);
    if (new_str == NULL) {
        return NULL;
    }

    return memcpy(new_str, s, len);
}

char *mdns_mem_strndup(const char *s, size_t n)
{
    if (s == NULL) {
        return NULL;
    }

    size_t len = strlen(s);
    if (len > n) {
        len = n;
    }

    char *new_str = mdns_mem_malloc(len + 1);
    if (new_str == NULL) {
        return NULL;
    }

    memcpy(new_str, s, len);
    new_str[len] = '\0';

    return new_str;
}

void *mdns_mem_task_malloc(size_t size)
{
    return malloc(size);
}

void mdns_mem_task_free(void *ptr)
{
    free(ptr);
}
