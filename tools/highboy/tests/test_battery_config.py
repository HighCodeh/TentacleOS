#!/usr/bin/env python3
# Copyright (c) 2025 HIGH CODE LLC
#
# TentacleOS is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# TentacleOS is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

"""Round-trip the production system config through cJSON and a host filesystem."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
CONFIG = ROOT / "firmware_p4/components/Service/storage_api"
PROFILE = ROOT / "firmware_p4/components/Drivers/bq25896/include"
CJSON = ROOT / "firmware_p4/managed_components/espressif__cjson/cJSON"
NOTICE = (CONFIG / "tos_config.c").read_text().split("#include", 1)[0]

MOCKS = {
    "esp_err.h": """#pragma once
#include <stddef.h>
typedef int esp_err_t;
enum {ESP_OK, ESP_FAIL, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, ESP_ERR_NOT_FOUND};
""",
    "esp_log.h": """#include <stdarg.h>
static inline void test_log(const char *tag, const char *format, ...) {(void)tag;(void)format;}
#define ESP_LOGI test_log
#define ESP_LOGW test_log
#define ESP_LOGE test_log
""",
    "haptic.h": """#define HAPTIC_FEEDBACK_INTENSITY 50
static inline void haptic_set_feedback_intensity(int value) {(void)value;}
""",
    "battery_service.h": """#include <stdint.h>
#include "esp_err.h"
esp_err_t battery_service_set_capacity(uint16_t capacity_mah);
""",
    "vfs_config.h": '#define VFS_MOUNT_POINT "."\n',
}

CODE = r"""
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "tos_config.c"
static bool fail_write;
static uint16_t applied_capacity = POWER_PROFILE_CAPACITY_MAH;
esp_err_t battery_service_set_capacity(uint16_t capacity_mah) {
  assert(capacity_mah == 2000 || capacity_mah == 2450);
  applied_capacity = capacity_mah;
  return ESP_OK;
}
esp_err_t storage_write_atomic(const char *path, const void *data, size_t length) {
  if (fail_write) return ESP_FAIL;
  FILE *file = fopen(path, "wb");
  assert(file != NULL);
  assert(fwrite(data, 1, length, file) == length);
  assert(fclose(file) == 0);
  return ESP_OK;
}
static void load_json(const char *json) {
  assert(storage_write_atomic(TOS_PATH_CONFIG_SYSTEM, json, strlen(json)) == ESP_OK);
  assert(tos_config_load(TOS_PATH_CONFIG_SYSTEM, NULL, "system") == ESP_OK);
}
int main(void) {
  assert(g_config_system.battery_capacity_mah == 2450);
  assert(mkdir("config", 0700) == 0);
  const char *invalid[] = {"{}", "{\"battery_capacity_mah\":0}",
    "{\"battery_capacity_mah\":3000}", "{\"battery_capacity_mah\":2000.5}",
    "{\"battery_capacity_mah\":-2000}", "{\"battery_capacity_mah\":\"2000\"}",
    "{\"battery_capacity_mah\":true}", "{\"battery_capacity_mah\":67536}"};
  for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
    g_config_system.battery_capacity_mah = 2000;
    load_json(invalid[i]);
    assert(g_config_system.battery_capacity_mah == 2450 && applied_capacity == 2450);
  }
  const uint16_t capacities[] = {2000, 2450};
  for (unsigned i = 0; i < sizeof(capacities) / sizeof(capacities[0]); i++) {
    g_config_system.volume = 63;
    assert(tos_config_set_battery_capacity(capacities[i]) == ESP_OK);
    assert(applied_capacity == capacities[i]);
    g_config_system.battery_capacity_mah = 0;
    g_config_system.volume = 0;
    assert(tos_config_load(TOS_PATH_CONFIG_SYSTEM, NULL, "system") == ESP_OK);
    assert(g_config_system.battery_capacity_mah == capacities[i]);
    assert(applied_capacity == capacities[i] && g_config_system.volume == 63);
  }
  fail_write = true;
  assert(tos_config_set_battery_capacity(2000) == ESP_FAIL);
  assert(g_config_system.battery_capacity_mah == 2450 && applied_capacity == 2450);
  fail_write = false;
  assert(tos_config_load(TOS_PATH_CONFIG_SYSTEM, NULL, "system") == ESP_OK);
  assert(g_config_system.battery_capacity_mah == 2450);
  assert(tos_config_set_battery_capacity(3000) == ESP_ERR_INVALID_ARG);
  assert(g_config_system.battery_capacity_mah == 2450 && applied_capacity == 2450);
  puts("PASS: battery config defaults, validation, both capacities across reload, other settings and failed-save rollback");
}
"""

with tempfile.TemporaryDirectory(prefix="battery-config-") as directory:
    tmp = Path(directory)
    for name, contents in MOCKS.items():
        (tmp / name).write_text(NOTICE + contents)
    (tmp / "test.c").write_text(NOTICE + CODE)
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=undefined", "-I", str(tmp), "-I", str(CONFIG),
        "-I", str(CONFIG / "include"), "-I", str(PROFILE), "-I", str(CJSON),
        str(tmp / "test.c"), str(CJSON / "cJSON.c"), "-o", str(tmp / "test"),
    ], check=True)
    subprocess.run([str(tmp / "test")], cwd=tmp, check=True)
