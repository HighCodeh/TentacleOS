#!/usr/bin/env python3
"""Parse the shipped remotes with the firmware parser, without IR hardware."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

from test_rf_ownership import function

ROOT = Path(__file__).resolve().parents[2]
IR = ROOT / "firmware_p4/components/Service/ir"


def test_browser(directory):
    source = (ROOT / "firmware_p4/components/Applications/ui/screens/infrared/ir_library_ui.c").read_text()
    assets = directory / "assets"
    sd = directory / "sd"
    assets.mkdir()
    (assets / "tv.ir").write_text("catalog")
    (assets / "ignore.txt").write_text("ignored")
    (assets / ".hidden.ir").write_text("ignored")
    (assets / "folder").mkdir()
    declarations = r"""#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#define LIB_DIR_MAX 48
#define LIB_PATH_MAX 256
#define LIB_NAME_MAX 96
#define LIB_DEPTH_MAX 8
#define ESP_LOGD(tag, ...) ((void)(tag))
static const char *TAG = "TEST";
typedef struct { char name[LIB_NAME_MAX]; bool directory; } library_entry_t;
static library_entry_t s_entries[LIB_DIR_MAX];
static char s_path[LIB_PATH_MAX];
static int s_count, s_depth, s_page;
static int s_dir_pages[LIB_DEPTH_MAX], s_dir_selections[LIB_DEPTH_MAX];
static bool s_missing, s_limited, s_is_builtin;
static void copy_string(char *dst, const char *src, size_t size) {
  assert(strlen(src) < size);
  memcpy(dst, src, strlen(src) + 1);
}
#ifdef strlcpy
#undef strlcpy
#endif
#define strlcpy copy_string
"""
    declarations += "#define LIB_ROOT " + json.dumps(str(sd)) + "\n"
    declarations += "#define LIB_BUILTIN_ROOT " + json.dumps(str(assets)) + "\n"
    for signature in ("static int compare_entries(", "static void scan_directory(",
                      "static void select_source("):
        declarations += function(source, signature) + "\n"
    declarations += r"""int main(void) {
  select_source(false);
  assert(s_missing && s_count == 0 && !s_is_builtin);
  select_source(true);
  assert(!s_missing && !s_limited && s_is_builtin && s_count == 2);
  assert(s_entries[0].directory && strcmp(s_entries[0].name, "folder") == 0);
  assert(!s_entries[1].directory && strcmp(s_entries[1].name, "tv.ir") == 0);
  s_depth = 3; s_page = 4; s_dir_pages[0] = 5; s_dir_selections[0] = 6;
  select_source(false);
  assert(s_depth == 0 && s_page == 0 && s_dir_pages[0] == 0 && s_dir_selections[0] == 0);
  assert(!s_is_builtin && s_missing);
  return 0;
}
"""
    test = directory / "browser.c"
    test.write_text(declarations)
    executable = directory / "browser"
    subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall",
                    "-Wextra", "-Werror", "-fsanitize=undefined", "-fno-sanitize-recover=all",
                    str(test), "-o", str(executable)], check=True, timeout=60)
    subprocess.run([str(executable)], check=True, timeout=30)
    print("PASS: IR library source switching, missing SD, filtering and folder sorting")


def main():
    with tempfile.TemporaryDirectory(prefix="highboy-ir-library-") as temporary:
        directory = Path(temporary)
        test_browser(directory)
        (directory / "driver").mkdir()
        (directory / "driver/rmt_types.h").write_text("""#pragma once
#include <stdint.h>
typedef union { struct { uint32_t duration0:15, level0:1, duration1:15, level1:1; }; uint32_t val; } rmt_symbol_word_t;
""")
        (directory / "pin_def.h").write_text("")
        (directory / "esp_err.h").write_text("""#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_SIZE 2
#define ESP_ERR_NO_MEM 3
""")
        (directory / "esp_log.h").write_text("""#define ESP_LOGE(tag, ...) ((void)(tag))
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGI(tag, ...) ((void)(tag))
""")
        (directory / "esp_heap_caps.h").write_text("""#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define heap_caps_malloc(size, caps) malloc(size)
#define heap_caps_realloc(ptr, size, caps) realloc(ptr, size)
""")
        (directory / "test.c").write_text(r"""#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "ir_file.h"
size_t ir_encode(const ir_data_t *d, rmt_symbol_word_t *s, size_t n) { (void)d; (void)s; (void)n; abort(); }
uint32_t ir_carrier_freq(ir_protocol_t p) { (void)p; abort(); }
esp_err_t ir_send(const ir_data_t *d) { (void)d; abort(); }
esp_err_t ir_send_raw(const rmt_symbol_word_t *s, size_t n, uint32_t f) { (void)s; (void)n; (void)f; abort(); }
int main(int argc, char **argv) {
  assert(argc > 1);
  for (int i = 1; i < argc; ++i) {
    FILE *input = fopen(argv[i], "rb");
    assert(input != NULL);
    assert(fseek(input, 0, SEEK_END) == 0);
    long length = ftell(input);
    assert(length > 0 && length <= IR_FILE_CONTENT_MAX);
    rewind(input);
    char *content = malloc((size_t)length + 1);
    assert(content != NULL);
    assert(fread(content, 1, (size_t)length, input) == (size_t)length);
    content[length] = 0;
    fclose(input);
    ir_file_t remote;
    ir_file_init(&remote);
    assert(ir_file_parse(content, &remote) == ESP_OK);
    assert(remote.count > 0);
    printf("PASS: %s: %zu supported signals\n", argv[i], remote.count);
    ir_file_free(&remote);
    assert(remote.signals == NULL && remote.count == 0);
    free(content);
  }
  return 0;
}
""")
        executable = directory / "test"
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall",
                        "-Wextra", "-Werror", "-fsanitize=undefined", "-fno-sanitize-recover=all",
                        "-I", str(directory), "-I", str(IR / "include"),
                        str(directory / "test.c"), str(IR / "ir_file.c"),
                        "-o", str(executable)], check=True, timeout=60)
        remotes = [ROOT / "firmware_p4/assets/ir" / name for name in ("tv.ir", "audio.ir", "ac.ir")]
        subprocess.run([str(executable), *map(str, remotes)], check=True, timeout=30)


if __name__ == "__main__":
    main()
