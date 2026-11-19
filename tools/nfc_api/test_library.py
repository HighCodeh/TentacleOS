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

"""Exercise real NFC file/vault linkage with a transactional NVS mock and SD faults."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
NFC = ROOT / "firmware_p4/components/Applications/nfc"
DRIVER = ROOT / "firmware_p4/components/Drivers/st25r3916/include"
NOTICE = (NFC / "nfc_library.c").read_text().split("#include", 1)[0]
with tempfile.TemporaryDirectory(prefix="nfc-library-") as tmp:
    work = Path(tmp)
    def write(name, text):
        (work / name).write_text(NOTICE + text)
    errors = ["ESP_OK", "ESP_FAIL", "ESP_ERR_INVALID_ARG", "ESP_ERR_INVALID_STATE",
              "ESP_ERR_INVALID_SIZE", "ESP_ERR_NOT_FOUND", "ESP_ERR_NOT_SUPPORTED",
              "ESP_ERR_INVALID_RESPONSE", "ESP_ERR_NO_MEM", "ESP_ERR_TIMEOUT",
              "ESP_ERR_NVS_NOT_FOUND"]
    write("esp_err.h", "#pragma once\ntypedef int esp_err_t;\n" +
          "".join(f"#define {name} {i}\n" for i, name in enumerate(errors)) +
          'static inline const char *esp_err_to_name(int e){(void)e;return "injected";}\n')
    write("esp_log.h", "#pragma once\n" + "".join(
        f"#define ESP_LOG{level}(tag,...) ((void)(tag))\n" for level in "EIWD"))
    write("esp_heap_caps.h", """#pragma once
#include <stdlib.h>
#include <assert.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
extern int fail_alloc;
static inline void *heap_caps_malloc(size_t n,int caps){assert(caps==3);return fail_alloc?NULL:malloc(n);}
static inline void *heap_caps_calloc(size_t n,size_t size,int caps){assert(caps==3);return fail_alloc?NULL:calloc(n,size);}
""")
    write("vfs_sdcard.h", "#pragma once\n#include <stdbool.h>\nbool vfs_sdcard_is_mounted(void);\n")
    write("storage_atomic.h", "#pragma once\n#include <stddef.h>\nint storage_write_atomic(const char*,const void*,size_t);\n")
    write("nvs.h", """#pragma once
#include <stddef.h>
#include "esp_err.h"
typedef int nvs_handle_t;
typedef int nvs_open_mode_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
int nvs_open(const char*,int,int*);
void nvs_close(int);
int nvs_get_blob(int,const char*,void*,size_t*);
int nvs_set_blob(int,const char*,const void*,size_t);
int nvs_erase_key(int,const char*);
int nvs_commit(int);
""")
    write("fault_fs.h", """#pragma once
FILE *fault_fopen(const char *,const char *);
int fault_unlink(const char *);
int fault_rename(const char *,const char *);
#define fopen fault_fopen
#define unlink fault_unlink
#define rename fault_rename
""")
    for header in ("nfc_archive.h", "nfc_library.h"):
        text = (NFC / "include" / header).read_text().replace("/sdcard/nfc/dumps", str(work / "dumps"))
        (work / header).write_text(text)
    archive = (NFC / "nfc_archive.c").read_text().replace('mkdir("/sdcard/nfc",', 'mkdir("' + str(work) + '",')
    archive = archive.replace('#include "storage_atomic.h"', '#include "storage_atomic.h"\n#include "fault_fs.h"')
    (work / "nfc_archive.c").write_text(archive)
    library = (NFC / "nfc_library.c").read_text().replace('#include "vfs_sdcard.h"', '#include "vfs_sdcard.h"\n#include "fault_fs.h"')
    (work / "nfc_library.c").write_text(library)
    command = [*shlex.split(os.environ.get("CC", "cc")), "-std=gnu11", "-Wall", "-Wextra",
               "-Werror", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
               "-I" + str(work), "-I" + str(NFC / "include"), "-I" + str(DRIVER),
               str(work / "nfc_archive.c"), str(work / "nfc_library.c"), str(NFC / "nfc_store.c"),
               str(Path(__file__).with_suffix(".c")), "-o", str(work / "test")]
    subprocess.run(command, check=True, timeout=60)
    subprocess.run([str(work / "test")], check=True, timeout=60)
