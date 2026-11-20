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

"""Exercise the production SD remount recovery without a physical card."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / 'firmware_p4/components/Service/storage_vfs/vfs_sdcard.c').read_text()
function = source[source.index('static esp_err_t reattach_card('):source.index('bool vfs_sdcard_try_acquire(')]
function = function.replace('static esp_err_t reattach_card(', 'esp_err_t vfs_sdcard_reattach_after_msc(')
notice = (ROOT / 'firmware_p4/components/Service/fatfs/patch_manifest.json.license').read_text()
code = '\n'.join('// ' + line for line in notice.splitlines()) + '\n' + r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_INVALID_STATE 2
#define SD_MSC_STOP_TIMEOUT_MS 1000
#define MMC_STOP_TRANSMISSION 12
#define SCF_CMD_AC 1
#define SCF_RSP_R1B 2
#define SCF_WAIT_BUSY 4
#define ESP_LOGW(tag, ...) ((void)0)
typedef struct { int opcode, flags, timeout_ms, error; } sdmmc_command_t;
typedef struct { struct { int slot; int (*do_transaction)(int,sdmmc_command_t*); } host; } sdmmc_card_t;
static int resumes, stops, failed_mounts, transport_error, command_error;
static int transact(int slot, sdmmc_command_t *cmd) {
  assert(slot==1 && cmd->opcode==MMC_STOP_TRANSMISSION);
  assert(cmd->flags==(SCF_CMD_AC|SCF_RSP_R1B|SCF_WAIT_BUSY));
  assert(cmd->timeout_ms==1000); stops++; cmd->error=command_error; return transport_error;
}
static sdmmc_card_t card = {.host={1,transact}};
static struct { bool mounted; sdmmc_card_t *card; } s_sdcard = {false,&card};
static int esp_vfs_fat_resume(const char *path) {
  assert(!strcmp(path,"/sdcard")); return ++resumes <= failed_mounts ? ESP_FAIL : ESP_OK;
}
""" + function + r"""
static void reset(void) { resumes=stops=failed_mounts=transport_error=command_error=0; s_sdcard.mounted=false; }
int main(void) {
  reset(); assert(vfs_sdcard_reattach_after_msc(NULL)==ESP_ERR_INVALID_STATE && !resumes);
  assert(vfs_sdcard_reattach_after_msc(&card)==ESP_OK && resumes==1 && stops==0 && s_sdcard.mounted);
  reset(); failed_mounts=1;
  assert(vfs_sdcard_reattach_after_msc(&card)==ESP_OK && resumes==2 && stops==1 && s_sdcard.mounted);
  reset(); failed_mounts=10;
  assert(vfs_sdcard_reattach_after_msc(&card)==ESP_FAIL && resumes==2 && stops==1 && !s_sdcard.mounted);
  reset(); failed_mounts=1; transport_error=ESP_FAIL;
  assert(vfs_sdcard_reattach_after_msc(&card)==ESP_FAIL && resumes==1 && stops==1 && !s_sdcard.mounted);
  reset(); failed_mounts=1; command_error=ESP_FAIL;
  assert(vfs_sdcard_reattach_after_msc(&card)==ESP_FAIL && resumes==1 && stops==1 && !s_sdcard.mounted);
}
"""
with tempfile.TemporaryDirectory(prefix='hb-sd-recovery-') as temporary:
    work=Path(temporary)
    (work/'test.c').write_text(code)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
print('PASS: bounded CMD12 recovery, command/transport errors and failed remount')
