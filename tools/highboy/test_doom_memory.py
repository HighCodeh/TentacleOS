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

from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "firmware_p4/components/Applications/doom"
NOTICE = (SOURCE / "doom_memory.c").read_text().split("#include", 1)[0]

with tempfile.TemporaryDirectory(prefix="highboy-doom-memory-") as temp:
    work = Path(temp)
    (work / "esp_err.h").write_text(NOTICE + "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_NO_MEM 1\n")
    (work / "esp_log.h").write_text(NOTICE + "#define ESP_LOGE(tag, ...) ((void)(tag))\n")
    (work / "esp_heap_caps.h").write_text(NOTICE + "#include <stddef.h>\n#define MALLOC_CAP_SPIRAM 1\n#define MALLOC_CAP_8BIT 2\nvoid *heap_caps_malloc(size_t, int);\n")
    fixture = r'''#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "info.h"
#include "sounds.h"
#include "doom_memory.h"
static unsigned calls, fail_at;
void *heap_caps_malloc(size_t size, int caps) {
  assert(caps == 3);
  return ++calls == fail_at ? NULL : malloc(size);
}
const state_t doom_initial_states[NUMSTATES] = {[0] = {.tics = 123}};
const mobjinfo_t doom_initial_mobjinfo[NUMMOBJTYPES] = {[0] = {.speed = 456}};
const sfxinfo_t doom_initial_sfx[NUMSFX] = {[1] = {.link = (sfxinfo_t *)&doom_initial_sfx[2], .priority = 42}};
const musicinfo_t doom_initial_music[NUMMUSIC] = {0};
int main(void) {
  assert(!states && !mobjinfo && !S_sfx && !S_music && !calls);
  for (fail_at = 1; fail_at <= 4; ++fail_at) {
    calls = 0;
    assert(doom_memory_init() == ESP_ERR_NO_MEM && !states && !mobjinfo && !S_sfx && !S_music);
  }
  fail_at = 0; calls = 0;
  assert(doom_memory_init() == ESP_OK && calls == 4);
  assert(!memcmp(states, doom_initial_states, sizeof(doom_initial_states)));
  assert(!memcmp(mobjinfo, doom_initial_mobjinfo, sizeof(doom_initial_mobjinfo)));
  assert(S_sfx[1].link == &S_sfx[2] && S_sfx[1].priority == 42);
  assert(!memcmp(S_music, doom_initial_music, sizeof(doom_initial_music)));
  S_sfx[1].priority = 99;
  assert(doom_initial_sfx[1].priority == 42);
  states[0].tics = 77; mobjinfo[0].speed = 88;
  assert(doom_initial_states[0].tics == 123 && doom_initial_mobjinfo[0].speed == 456);
  assert(doom_memory_init() == ESP_OK && calls == 4 && states[0].tics == 77);
  free(states); free(mobjinfo); free(S_sfx); free(S_music);
}
'''
    (work / "test.c").write_text(NOTICE + fixture)
    binary = work / "test"
    subprocess.run([os.environ.get("CC", "cc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-I", str(work), "-I", str(SOURCE),
                    str(work / "test.c"), str(SOURCE / "doom_memory.c"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: DOOM tables allocate only on demand, PSRAM-only, exact copies, failure cleanup and idempotence")
