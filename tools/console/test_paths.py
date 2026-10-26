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

"""Exercise production path handling with explicit filesystem/UI/HID fakes."""
import os
import re
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SERVICE = ROOT / "firmware_p4/components/Service"
UI = ROOT / "firmware_p4/components/Applications/ui"
NOTICE = (SERVICE / "storage_api/include/tos_path.h").read_text().split("#ifndef", 1)[0]


def function(source, signature):
    pattern = r"\s+".join(re.escape(token) for token in signature.split())
    match = re.search(pattern, source)
    if match is None:
        raise AssertionError("Production function not found: " + signature)
    start = match.start()
    opening = source.index("{", start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


COMMON = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include "tos_path.h"
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
static int stat_calls, open_calls, dir_calls, entry_index;
static bool stat_failure;
static mode_t stat_mode = S_IFDIR;
static const char *entry_name;
static unsigned char entry_type;
static char last_path[1024];
static int fake_stat(const char *path, struct stat *out) {
  stat_calls++;
  assert(strlen(path) < sizeof(last_path));
  strcpy(last_path, path);
  if (stat_failure) return -1;
  memset(out, 0, sizeof(*out));
  out->st_mode = stat_mode;
  return 0;
}
static FILE *fake_fopen(const char *path, const char *mode) {
  (void)mode; open_calls++; strcpy(last_path, path); return NULL;
}
static DIR *fake_opendir(const char *path) {
  dir_calls++; strcpy(last_path, path); return (DIR *)(uintptr_t)1;
}
static int fake_closedir(DIR *dir) { (void)dir; return 0; }
static struct dirent *fake_readdir(DIR *dir) {
  (void)dir;
  static struct dirent ent;
  if (entry_index++ || entry_name == NULL) return NULL;
  strcpy(ent.d_name, entry_name); ent.d_type = entry_type; return &ent;
}
#define stat(path, out) fake_stat(path, out)
#define fopen fake_fopen
#define opendir fake_opendir
#define closedir fake_closedir
#define readdir fake_readdir
#define strlcpy test_copy
static size_t test_copy(char *dst, const char *src, size_t cap) {
  size_t n = strlen(src);
  if (cap) { size_t count = n < cap - 1 ? n : cap - 1; memcpy(dst, src, count); dst[count] = 0; }
  return n;
}
"""


def console_case():
    source = (SERVICE / "console/commands/cmd_fs.c").read_text()
    declarations = source[source.index("#define FS_PATH_CAPACITY"):source.index("// LS Args")]
    declarations += r"""
#include "cJSON.h"
struct arg_str { int count; const char *sval[1]; };
struct arg_lit { int count; };
static struct { struct arg_str *path; struct arg_lit *json; void *end; } s_ls_args;
static struct { struct arg_str *path; void *end; } s_cd_args, s_cat_args;
static int arg_parse(int argc, char **argv, void **args) { (void)argc; (void)argv; (void)args; return 0; }
static void arg_print_errors(FILE *f, void *end, const char *cmd) { (void)f; (void)end; (void)cmd; }
"""
    declarations += "\n".join(function(source, "static int " + name + "(")
                               for name in ("cmd_cd", "cmd_ls", "cmd_cat"))
    return declarations + r"""
int main(void) {
  char out[8];
  assert(tos_path_join("/a", "1234", out, sizeof(out)) == ESP_OK);
  assert(strcmp(out, "/a/1234") == 0);
  assert(tos_path_join("/a", "12345", out, sizeof(out)) == ESP_ERR_INVALID_SIZE && out[0] == 0);
  assert(tos_path_join("/", "a", out, sizeof(out)) == ESP_OK && strcmp(out, "/a") == 0);
  assert(tos_path_join("/a/", "b", out, sizeof(out)) == ESP_OK && strcmp(out, "/a/b") == 0);
  assert(tos_path_join("", "", out, 1) == ESP_OK && out[0] == 0);
  assert(tos_path_join("", "a", out, 1) == ESP_ERR_INVALID_SIZE && out[0] == 0);
  assert(tos_path_join(NULL, "a", out, sizeof(out)) == ESP_ERR_INVALID_ARG && out[0] == 0);
  assert(tos_path_join("", "a", NULL, 0) == ESP_ERR_INVALID_ARG);
  char nonterminated[8]; memset(nonterminated, 'x', sizeof(nonterminated));
  assert(tos_path_join("", nonterminated, out, sizeof(out)) == ESP_ERR_INVALID_SIZE);
  char exact[FS_PATH_CAPACITY]; memset(exact, 'x', sizeof(exact)); exact[0]='/'; exact[sizeof(exact)-1]=0;
  char long_path[FS_PATH_CAPACITY + 1]; memset(long_path, 'x', sizeof(long_path)); long_path[0]='/'; long_path[sizeof(long_path)-1]=0;
  char resolved[FS_PATH_CAPACITY];
  assert(resolve_path(exact, resolved, sizeof(resolved)) == ESP_OK && !strcmp(exact,resolved));
  assert(resolve_path(long_path, resolved, sizeof(resolved)) == ESP_ERR_INVALID_SIZE && resolved[0]==0);
  assert(resolve_path(NULL, resolved, sizeof(resolved)) == ESP_OK && !strcmp(resolved,"/assets"));
  assert(resolve_path("", resolved, sizeof(resolved)) == ESP_OK && !strcmp(resolved,"/assets"));
  struct arg_str arg = {1,{long_path}}; struct arg_lit json={0};
  s_cd_args.path=s_cat_args.path=s_ls_args.path=&arg; s_ls_args.json=&json;
  assert(cmd_cd(0,NULL)==1 && stat_calls==0 && !strcmp(s_cwd,"/assets"));
  assert(cmd_cat(0,NULL)==1 && open_calls==0);
  assert(cmd_ls(0,NULL)==1 && dir_calls==0);
  arg.sval[0]=exact; assert(cmd_cd(0,NULL)==0 && stat_calls==1 && !strcmp(s_cwd,exact));
  arg.sval[0]="child"; assert(cmd_cd(0,NULL)==1 && stat_calls==1 && !strcmp(s_cwd,exact));
  assert(cmd_cat(0,NULL)==1 && open_calls==0);
  assert(cmd_ls(0,NULL)==1 && dir_calls==0);
  arg.sval[0]=".."; assert(cmd_cd(0,NULL)==0 && !strcmp(s_cwd,"/"));
  arg.sval[0]="child"; assert(cmd_cd(0,NULL)==0 && !strcmp(s_cwd,"/child"));
  arg.sval[0]="."; int before=stat_calls; assert(cmd_cd(0,NULL)==0 && stat_calls==before);
  arg.count=0; entry_name="gone"; stat_failure=true;
  assert(cmd_ls(0,NULL)==1 && dir_calls==1);
  return 0;
}
"""


def browser_case():
    source = (UI / "screens/badusb/badusb_menu_ui.c").read_text()
    declarations = r"""
#define MAX_PAYLOADS 4
#define PL_PATH_LEN 192
#define PL_NAME_LEN 48
#define BADUSB_SCAN_MAX_DEPTH 3
static int s_pl_count;
static char s_pl_path[MAX_PAYLOADS][PL_PATH_LEN], s_pl_name[MAX_PAYLOADS][PL_NAME_LEN];
static bool s_pl_is_asset[MAX_PAYLOADS];
"""
    declarations += function(source,"static bool is_ducky_script(")
    declarations += function(source,"static void scan_dir_into_depth(")
    return declarations + r"""
int main(void) {
  char base[PL_PATH_LEN]; memset(base,'b',sizeof(base)); base[0]='/'; base[sizeof(base)-1]=0;
  entry_name="p.txt"; entry_type=DT_REG;
  scan_dir_into_depth(base,false,0); assert(s_pl_count==0 && dir_calls==1);
  entry_index=0; entry_type=DT_DIR;
  scan_dir_into_depth(base,false,0); assert(s_pl_count==0 && dir_calls==2);
  base[PL_PATH_LEN-1-1-strlen(entry_name)]=0;
  entry_index=0; entry_type=DT_REG;
  scan_dir_into_depth(base,false,0); assert(s_pl_count==1);
  assert(strlen(s_pl_path[0])==PL_PATH_LEN-1);
  assert(!strcmp(s_pl_path[0]+strlen(base),"/p.txt"));
  entry_index=0; scan_dir_into_depth("/sdcard/badusb",false,0);
  assert(s_pl_count==2 && !strcmp(s_pl_path[1],"/sdcard/badusb/p.txt"));
  return 0;
}
"""


def theme_case():
    selector = (UI / "screens/theme/theme_selector_ui.c").read_text()
    loader = (UI / "ui_theme.c").read_text()
    declarations = r"""
#define THEME_PATH_MAX 128
#define ASSET_DIR_PATH_MAX 160
#define THEMES_DIR "/sdcard/themes"
#define THEME_COUNT 2
static const char *theme_names[] = {"default", "cyber_blue"};
static struct {char theme[32];} g_config_screen;
#define TNAME_MAX 32
#define MAX_THEMES 24
#define THEME_FILE_JSON "theme.json"
#define THEME_FILE_CONF "theme.conf"
static const struct {const char *label; uint32_t accent;} THEMES[]={{"Default",0},{"Cyber",1}};
typedef struct {char name[TNAME_MAX], label[TNAME_MAX];uint32_t accent;bool builtin;int flash_idx;} theme_entry_t;
static theme_entry_t s_entries[MAX_THEMES];
static int s_count;
static uint32_t read_sd_accent(const char *name) {(void)name;return 0;}
static int theme_idx, loads, reads, unloads;
static void ui_theme_load_idx(int i) { (void)i; loads++; }
static char *read_file_alloc(const char *path) { reads++; strcpy(last_path,path); return NULL; }
static void parse_theme_conf(char *s) { (void)s; }
static void parse_theme_json(char *s) { (void)s; }
static void assets_unload_sd(void) { unloads++; }
static int assets_load_from_sd(const char *path, const char *key) { (void)path; (void)key; return 0; }
static void assets_manager_warm(const char *key) { (void)key; }
"""
    declarations += function(selector,"static esp_err_t theme_file_path(")
    declarations += function(selector,"static bool has_theme_file(")
    declarations += function(selector,"static void build_entries(")
    declarations += function(loader,"esp_err_t ui_theme_load_from_name(")
    return declarations + r"""
int main(void) {
  char name[128]; memset(name,'t',sizeof(name)); name[sizeof(name)-1]=0;
  char path[THEME_PATH_MAX];
  assert(theme_file_path(name,"theme.json",path,sizeof(path))==ESP_ERR_INVALID_SIZE && path[0]==0);
  name[THEME_PATH_MAX-strlen(THEMES_DIR)-strlen("/theme.json")-2]=0;
  assert(theme_file_path(name,"theme.json",path,sizeof(path))==ESP_OK && strlen(path)==sizeof(path)-1);
  assert(ui_theme_load_from_name(name)==ESP_ERR_INVALID_SIZE && reads==0 && unloads==0 && loads==0);
  name[32]=0; assert(ui_theme_load_from_name(name)==ESP_ERR_INVALID_SIZE && reads==0);
  name[31]=0; assert(ui_theme_load_from_name(name)==ESP_OK && reads==2 && loads==1);
  assert(strstr(last_path,name)!=NULL && strstr(last_path,"theme.json")!=NULL);
  assert(ui_theme_load_from_name(NULL)==ESP_OK && loads==2);
  stat_mode=S_IFREG;entry_name=name;entry_type=DT_DIR;
  name[31]='t';name[32]=0;entry_index=0;
  build_entries();assert(s_count==THEME_COUNT && stat_calls==0);
  name[31]=0;entry_index=0;
  build_entries();assert(s_count==THEME_COUNT+1 && stat_calls==1);
  assert(!strcmp(s_entries[THEME_COUNT].name,name));
  return 0;
}
"""


def ingress_case():
    source = (SERVICE / "console/commands/cmd_badusb.c").read_text()
    host = (SERVICE / "host_link/host_link_theme.c").read_text()
    declarations = r"""
#define BADUSB_ASSET_PATH_MAX 128
#define BADUSB_ASSET_DIR "storage/bad_usb_scripts/"
#define DUCKY_OUTPUT_USB 0
#define SPI_STATUS_INVALID_ARG 1
#define SPI_STATUS_OK 0
#define TOS_PATH_CONFIG_SCREEN "screen"
struct arg_str {int count; const char *sval[1];};
struct arg_lit {int count;};
static struct {struct arg_str *asset, *file; struct arg_lit *ble; void *end;} s_run_args;
static int starts, executions, saves, schedules;
static struct {char theme[32];} g_config_screen;
static char s_theme_name[32];
static int arg_parse(int argc,char **argv,void **args) {(void)argc;(void)argv;(void)args;return 0;}
static void arg_print_errors(FILE *f,void *end,const char *cmd) {(void)f;(void)end;(void)cmd;}
static bool start_ble_target(void) {starts++;return true;}
static esp_err_t ensure_badusb_ready(void) {starts++;return ESP_OK;}
static void ducky_set_output_mode(int mode) {(void)mode;}
static esp_err_t ducky_run_from_assets(const char *path) {executions++;strcpy(last_path,path);return ESP_OK;}
static esp_err_t ducky_run_from_sdcard(const char *path) {return ducky_run_from_assets(path);}
static void bad_ble_deinit(void) {}
static const char *esp_err_to_name(esp_err_t err) {(void)err;return "error";}
static void apply_theme_cb(void *arg) {(void)arg;}
static void ui_async_call(void (*cb)(void *),void *arg) {(void)cb;(void)arg;schedules++;}
static void tos_config_save(const char *path,const char *section) {(void)path;(void)section;saves++;}
"""
    declarations += function(source, "static int subcmd_run(")
    declarations += function(host, "uint8_t host_theme_handle(")
    return declarations + r"""
int main(void) {
  char name[256];memset(name,'a',sizeof(name));name[sizeof(name)-1]=0;
  struct arg_str asset={1,{name}},file={0,{NULL}};struct arg_lit ble={0};
  s_run_args.asset=&asset;s_run_args.file=&file;s_run_args.ble=&ble;
  assert(subcmd_run(0,NULL)==1 && starts==0 && executions==0);
  ble.count=1;assert(subcmd_run(0,NULL)==1 && starts==0 && executions==0);
  name[BADUSB_ASSET_PATH_MAX-strlen(BADUSB_ASSET_DIR)-1]=0;
  assert(subcmd_run(0,NULL)==0 && starts==1 && executions==1);
  assert(strlen(last_path)==BADUSB_ASSET_PATH_MAX-1);
  memset(name,'t',sizeof(name));name[31]=0;
  assert(host_theme_handle(0,(const uint8_t *)name,31,NULL,0,NULL)==SPI_STATUS_OK);
  assert(saves==1 && schedules==1 && !strcmp(g_config_screen.theme,name));
  assert(host_theme_handle(0,(const uint8_t *)name,32,NULL,0,NULL)==SPI_STATUS_OK);
  assert(saves==2 && schedules==2);
  char previous[32];strcpy(previous,g_config_screen.theme);
  name[31]='t';name[32]=0;
  assert(host_theme_handle(0,(const uint8_t *)name,32,NULL,0,NULL)==SPI_STATUS_INVALID_ARG);
  assert(saves==2 && schedules==2 && !strcmp(g_config_screen.theme,previous));
  name[1]=0;
  assert(host_theme_handle(0,(const uint8_t *)name,31,NULL,0,NULL)==SPI_STATUS_INVALID_ARG);
  assert(saves==2 && schedules==2 && !strcmp(g_config_screen.theme,previous));
  return 0;
}
"""


def main():
    with tempfile.TemporaryDirectory(prefix="tos-paths-") as temporary:
        work = Path(temporary)
        (work / "esp_err.h").write_text(NOTICE + "#pragma once\ntypedef int esp_err_t;\n"
            "#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n#define ESP_ERR_INVALID_SIZE 2\n")
        cjson = ROOT / "tools/dependencies/build/managed_components/espressif__cjson/cJSON"
        for name, case in (("console", console_case), ("badusb", browser_case), ("theme", theme_case), ("ingress", ingress_case)):
            source = work / (name + ".c")
            source.write_text(NOTICE + COMMON + case())
            executable = work / name
            subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=gnu11", "-Wall", "-Wextra",
                "-Werror", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-parameter",
                "-Wno-deprecated-declarations", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                "-I", str(work), "-I", str(SERVICE / "storage_api/include"), "-I", str(cjson),
                str(source), str(cjson / "cJSON.c"), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
    print("PASS: bounded paths, console no-I/O failures, unchanged cwd, BadUSB scans and theme limits")


if __name__ == "__main__":
    main()
