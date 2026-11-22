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
enum {ESP_OK, ESP_FAIL, ESP_ERR_INVALID_ARG, ESP_ERR_NO_MEM, ESP_ERR_NOT_FOUND, ESP_ERR_INVALID_STATE};
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
#include "power_profile.h"
""" + "#define BATTERY_SERVICE_PROFILE_ID_SIZE" + (PROFILE / "battery_service.h").read_text().split(
        "#define BATTERY_SERVICE_PROFILE_ID_SIZE", 1)[1].split("/**", 1)[0] + """
const battery_service_profile_t *battery_service_profile_find(const char *);
const battery_service_profile_t *battery_service_profile_get(void);
const battery_service_profile_t *battery_service_profile_at(uint32_t);
esp_err_t battery_service_set_profile(const char *);
""",
    "vfs_config.h": '#define VFS_MOUNT_POINT "."\n',
}

SERVICE = (PROFILE.parent / "battery_service.c").read_text()
def function(source, signature):
    start = source.index(signature)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]
CATALOG = "\n".join(line for line in SERVICE.splitlines() if line.startswith("#define PACK_")) + "\n"
CATALOG += SERVICE[SERVICE.index("static const battery_service_profile_t BATTERY_PROFILES[]"):
                   SERVICE.index("static int filter_soc(")]
for signature in ("const battery_service_profile_t *battery_service_profile_at(",
                  "const battery_service_profile_t *battery_service_profile_find(",
                  "const battery_service_profile_t *battery_service_profile_get(",
                  "esp_err_t battery_service_set_profile("):
    CATALOG += function(SERVICE, signature) + "\n"
CONSOLE = (ROOT / "firmware_p4/components/Service/console/commands/cmd_battery.c").read_text()
COMMAND = function(CONSOLE, "static void show_profile(") + function(CONSOLE, "static int profile_command(")

CODE = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/stat.h>
#include "tos_config.c"
#include "catalog.h"
static bool fail_write, nested_write;
static unsigned writes, infos, errors, warnings, allocation, fail_at;
static void led_signal_info(void) {++infos;}
static void led_signal_error(void) {++errors;}
static void led_signal_warning(void) {++warnings;}
static const char *esp_err_to_name(int error) {(void)error;return "fixture";}
#include "profile_command.h"
static void *test_alloc(size_t size) {
  return ++allocation == fail_at ? NULL : malloc(size);
}
esp_err_t storage_write_atomic(const char *path, const void *data, size_t length) {
  if (fail_write) return ESP_FAIL;
  if (nested_write) {
    assert(tos_config_save(path,"system")==ESP_ERR_INVALID_STATE);
    assert(tos_config_set_battery_profile("generic_2000")==ESP_ERR_INVALID_STATE);
    assert(tos_config_load(path,NULL,"system")==ESP_ERR_INVALID_STATE);
    assert(!strcmp(battery_service_profile_get()->id,"generic_2450"));
  }
  FILE *file = fopen(path, "wb");
  assert(file != NULL);
  assert(fwrite(data, 1, length, file) == length);
  assert(fclose(file) == 0);
  ++writes;
  return ESP_OK;
}
static void load_json(const char *json) {
  assert(storage_write_atomic(TOS_PATH_CONFIG_SYSTEM, json, strlen(json)) == ESP_OK);
  assert(tos_config_load(TOS_PATH_CONFIG_SYSTEM, NULL, "system") == ESP_OK);
}
static void expect(const char *id, uint16_t capacity) {
  assert(!strcmp(g_config_system.battery_profile,id));
  assert(!strcmp(battery_service_profile_get()->id,id));
  assert(g_config_system.battery_capacity_mah==capacity);
}
int main(void) {
  expect("generic_2450",2450);
  assert(mkdir("config", 0700) == 0);
  const char *invalid[] = {"{}", "{\"battery_capacity_mah\":0}",
    "{\"battery_capacity_mah\":3000}", "{\"battery_capacity_mah\":2000.5}",
    "{\"battery_capacity_mah\":-2000}", "{\"battery_capacity_mah\":\"2000\"}",
    "{\"battery_capacity_mah\":true}", "{\"battery_capacity_mah\":67536}",
    "{\"battery_profile\":0}","{\"battery_profile\":\"invalid\"}"};
  for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
    load_json(invalid[i]);expect("generic_2450",2450);
  }
  load_json("{\"battery_capacity_mah\":2000}");expect("generic_2000",2000);
  load_json("{\"battery_profile\":\"invalid\",\"battery_capacity_mah\":2000}");expect("generic_2000",2000);
  load_json("{\"battery_profile\":\"knlypow_604858\",\"battery_capacity_mah\":2450}");expect("knlypow_604858",2000);
  for(unsigned i=0;battery_service_profile_at(i);i++) {
    const battery_service_profile_t *profile=battery_service_profile_at(i);
    g_config_system.volume=63;
    assert(tos_config_set_battery_profile(profile->id)==ESP_OK);
    assert(tos_config_load(TOS_PATH_CONFIG_SYSTEM,NULL,"system")==ESP_OK);
    expect(profile->id,profile->capacity_mah);assert(g_config_system.volume==63);
  }
  assert(tos_config_set_battery_profile("generic_2450")==ESP_OK);
  fail_write=true;
  assert(tos_config_set_battery_profile("knlypow_604858")==ESP_FAIL);
  expect("generic_2450",2450);
  fail_write=false;
  assert(tos_config_load(TOS_PATH_CONFIG_SYSTEM,NULL,"system")==ESP_OK);
  expect("generic_2450",2450);
  unsigned before=writes;
  assert(tos_config_set_battery_profile(NULL)==ESP_ERR_INVALID_ARG);
  assert(tos_config_set_battery_profile("bad")==ESP_ERR_INVALID_ARG && writes==before);
  cJSON_Hooks hooks={.malloc_fn=test_alloc,.free_fn=free};
  cJSON_InitHooks(&hooks);
  bool completed=false;
  for(fail_at=1;fail_at<100;fail_at++) {
    allocation=0;
    int result=tos_config_set_battery_profile("knlypow_604858");
    if(result==ESP_OK) {completed=true;break;}
    expect("generic_2450",2450);assert(writes==before);
  }
  cJSON_InitHooks(NULL);assert(completed && writes==before+1);
  assert(tos_config_load(TOS_PATH_CONFIG_SYSTEM,NULL,"system")==ESP_OK);
  expect("knlypow_604858",2000);
  assert(tos_config_set_battery_profile("generic_2450")==ESP_OK);
  nested_write=true;
  assert(tos_config_set_battery_profile("knlypow_604858")==ESP_OK);
  nested_write=false;expect("knlypow_604858",2000);
  assert(tos_config_load(TOS_PATH_CONFIG_SYSTEM,NULL,"system")==ESP_OK);
  expect("knlypow_604858",2000);
  char *list[]={"battery","profiles",NULL};
  assert(profile_command(2,list)==0);
  char *show[]={"battery","profile",NULL};
  assert(profile_command(2,show)==0);
  char *set[]={"battery","profile","knlypow_604858",NULL};
  assert(profile_command(3,set)==0 && infos==1);expect("knlypow_604858",2000);
  set[2]="bogus";before=writes;
  assert(profile_command(3,set)==1 && errors==1 && writes==before);
  set[2]="knlypow_604858";fail_write=true;
  assert(profile_command(3,set)==1 && errors==2 && infos==1);expect("knlypow_604858",2000);
  fail_write=false;
  assert(profile_command(3,list)==1 && warnings==1);
  puts("PASS: real battery catalog/config/console, legacy migration, profile precedence, every serializer allocation failure, atomic-save failure, overlapping system writes/reload, reload and unchanged selection on errors");
}
"""

with tempfile.TemporaryDirectory(prefix="battery-config-") as directory:
    tmp = Path(directory)
    for name, contents in MOCKS.items():
        (tmp / name).write_text(NOTICE + contents)
    (tmp / "catalog.h").write_text(NOTICE + CATALOG)
    (tmp / "profile_command.h").write_text(NOTICE + COMMAND)
    (tmp / "test.c").write_text(NOTICE + CODE)
    subprocess.run([
        os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=undefined", "-I", str(tmp), "-I", str(CONFIG),
        "-I", str(CONFIG / "include"), "-I", str(PROFILE), "-I", str(CJSON),
        str(tmp / "test.c"), str(CJSON / "cJSON.c"), "-o", str(tmp / "test"),
    ], check=True)
    subprocess.run([str(tmp / "test")], cwd=tmp, check=True)
