// Copyright (c) 2025 HIGH CODE LLC
//
// TentacleOS is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// TentacleOS is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with TentacleOS. If not, see <https://www.gnu.org/licenses/>.

#include "test_platform.h"
static FILE *test_open(const char *,const char *);
static int test_stat(const char *,struct stat *);
static int test_seek(FILE *,long,int);
static long test_tell(FILE *);
static size_t test_read(void *,size_t,size_t,FILE *);
static int test_file_error(FILE *);
static int test_close(FILE *);
static int test_remove(const char *);
static void *test_allocate(size_t);
static void test_free(void *);
#define fopen test_open
#define stat(path, output) test_stat(path, output)
#define fseek test_seek
#define ftell test_tell
#define fread test_read
#define ferror test_file_error
#define fclose test_close
#define remove test_remove
#define malloc test_allocate
#define free test_free
#include "ota_service.c"
#undef fopen
#undef stat
#undef fseek
#undef ftell
#undef fread
#undef ferror
#undef fclose
#undef remove
#undef malloc
#undef free

typedef enum {NORMAL,NO_SD,NO_FILE,DIRECTORY,OPEN_ERROR,SEEK_END_ERROR,SEEK_SET_ERROR,TELL_ERROR,
  EMPTY,LARGE,NO_PARTITION,NO_RUNNING,SAME_PARTITION,NO_MEMORY,BEGIN_ERROR,SHORT_READ,READ_ERROR,
  WRITE_ERROR,END_ERROR,BOOT_ERROR,ABORT_ERROR,POWER_ERROR,POWER_RELEASE_ERROR} fault_t;
static fault_t s_fault;
static unsigned s_power,s_begins,s_writes,s_ends,s_aborts,s_boots,s_restarts,s_closes,s_removes,s_yields,s_syncs;
static unsigned s_confirms,s_health_waits;
static size_t s_written,s_file_offset;
static bool s_file_open,s_handle_live,s_is_available=true,s_assets=true,s_renderer=true;
static void *s_buffer;
static esp_ota_img_states_t s_ota_state=ESP_OTA_IMG_VALID;
static esp_err_t s_state_error,s_confirm_error;
static uint32_t s_beat;
static bool s_reenter;
static const esp_partition_t RUNNING={.address=0x20000,.size=0x2d0000,.label="ota_0"};
static const esp_partition_t NEXT={.address=0x2f0000,.size=0x2d0000,.label="ota_1"};
const char *esp_err_to_name(esp_err_t error) {(void)error;return "fixture error";}
bool sd_is_mounted(void) {return s_fault!=NO_SD;}
static int test_stat(const char *path,struct stat *out) {
  assert(strcmp(path,OTA_UPDATE_PATH)==0);if(s_fault==NO_FILE || !s_is_available)return -1;
  memset(out,0,sizeof(*out));out->st_mode=s_fault==DIRECTORY?S_IFDIR:S_IFREG;out->st_size=8205;return 0;
}
static FILE *test_open(const char *path,const char *mode) {
  assert(strcmp(path,OTA_UPDATE_PATH)==0 && strcmp(mode,"rb")==0 && !s_file_open && s_power==1);
  if (s_fault == OPEN_ERROR) {
    return NULL;
  }
  s_file_open = true;
  return (FILE *)1;
}
static int test_seek(FILE *file,long offset,int whence) {
  assert(file==(FILE *)1 && offset==0);
  return (s_fault==SEEK_END_ERROR && whence==SEEK_END) || (s_fault==SEEK_SET_ERROR && whence==SEEK_SET)?-1:0;
}
static long test_tell(FILE *file) {
  assert(file==(FILE *)1);return s_fault==TELL_ERROR?-1:s_fault==EMPTY?0:s_fault==LARGE?0x2d0001:8205;
}
static size_t test_read(void *out,size_t size,size_t count,FILE *file) {
  assert(file==(FILE *)1 && s_handle_live && s_power==1 && out==s_buffer && size==1);
  if(s_fault==SHORT_READ)return count-1;
  memset(out,0x5a,count);s_file_offset+=count;return count;
}
static int test_file_error(FILE *file) {assert(file==(FILE *)1);return s_fault==READ_ERROR;}
static int test_close(FILE *file) {assert(file==(FILE *)1 && s_file_open);s_file_open=false;++s_closes;return 0;}
static int test_remove(const char *path) {assert(strcmp(path,OTA_UPDATE_PATH)==0 && !s_file_open && !s_buffer);++s_removes;return 0;}
static void *test_allocate(size_t size) {
  assert(size==4096 && !s_handle_live && !s_buffer);
  if (s_fault == NO_MEMORY) {
    return NULL;
  }
  s_buffer = malloc(size);
  return s_buffer;
}
static void test_free(void *p) {if(p==s_buffer)s_buffer=NULL;free(p);}
esp_err_t power_manager_no_sleep_acquire(void) {assert(s_power==0);if(s_fault==POWER_ERROR)return ESP_FAIL;++s_power;return ESP_OK;}
esp_err_t power_manager_no_sleep_release(void) {assert(s_power==1 && !s_handle_live);if(s_fault==POWER_RELEASE_ERROR)return ESP_FAIL;--s_power;return ESP_OK;}
const esp_partition_t *esp_ota_get_next_update_partition(const void *p) {(void)p;return s_fault==NO_PARTITION?NULL:s_fault==SAME_PARTITION?&RUNNING:&NEXT;}
const esp_partition_t *esp_ota_get_running_partition(void) {return s_fault==NO_RUNNING?NULL:&RUNNING;}
esp_err_t esp_ota_begin(const esp_partition_t *part,size_t size,esp_ota_handle_t *out) {
  assert(part==&NEXT && size==OTA_WITH_SEQUENTIAL_WRITES && s_buffer && !s_handle_live && s_power==1);++s_begins;
  if (s_fault == BEGIN_ERROR) {
    return ESP_FAIL;
  }
  s_handle_live = true;
  *out = 1;
  return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t handle,const void *data,size_t size) {
  assert(handle==1 && s_handle_live && s_power==1 && data==s_buffer);++s_writes;
  if (s_fault == WRITE_ERROR || s_fault == ABORT_ERROR) {
    return ESP_FAIL;
  }
  s_written += size;
  return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t handle) {
  assert(handle==1 && s_handle_live && s_written==8205);++s_ends;s_handle_live=false;
  return s_fault==END_ERROR?ESP_FAIL:ESP_OK;
}
esp_err_t esp_ota_abort(esp_ota_handle_t handle) {
  assert(handle==1 && s_handle_live && s_power==1);++s_aborts;
  if (s_fault == ABORT_ERROR) {
    return ESP_FAIL;
  }
  s_handle_live = false;
  return ESP_OK;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *part) {
  assert(part==&NEXT && !s_handle_live && s_ends==1);++s_boots;return s_fault==BOOT_ERROR?ESP_FAIL:ESP_OK;
}
void esp_restart(void) {assert(s_boots==1 && s_power==1 && !s_file_open && !s_buffer && !s_handle_live);++s_restarts;}
void vTaskDelay(uint32_t ticks) {
  if(ticks==1){++s_yields;return;}
  assert(ticks==3000);++s_health_waits;if(s_renderer)s_beat+=4;
}
bool storage_assets_is_mounted(void) {return s_assets;}
uint32_t ui_render_beat(void) {return s_beat;}
uint8_t *storage_assets_load_file(const char *path,size_t *out_size) {
  assert(strcmp(path,VERSION_JSON_PATH)==0);const char json[]="{\"version\":\"old\"}";
  *out_size=sizeof(json)-1;uint8_t *out=malloc(sizeof(json));memcpy(out,json,sizeof(json));return out;
}
esp_err_t storage_assets_write_file(const char *path,const char *json) {
  assert(strcmp(path,VERSION_JSON_PATH)==0 && strstr(json,FIRMWARE_VERSION));++s_syncs;return ESP_OK;
}
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p,esp_ota_img_states_t *out) {assert(p==&RUNNING);*out=s_ota_state;return s_state_error;}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) {++s_confirms;return s_confirm_error;}
static void progress(int percent,const char *message) {
  assert(percent>=0 && percent<=100 && message);
  if(s_reenter){s_reenter=false;assert(ota_start_update(NULL)==ESP_ERR_INVALID_STATE);}
}
static void reset(void) {
  assert(!s_handle_live && !s_file_open && !s_buffer);s_fault=NORMAL;s_power=0;
  s_begins=s_writes=s_ends=s_aborts=s_boots=s_restarts=s_closes=s_removes=s_yields=s_syncs=0;
  s_confirms=s_health_waits=0;s_written=s_file_offset=0;
  s_has_pending_handle=s_has_power_hold=false;s_state=OTA_STATE_IDLE;atomic_flag_clear(&s_is_updating);
  s_state_error=s_confirm_error=ESP_OK;s_assets=s_renderer=true;s_ota_state=ESP_OTA_IMG_VALID;s_beat=0;
}
int main(void) {
  for(fault_t fault=NORMAL;fault<=POWER_RELEASE_ERROR;++fault) {
    reset();s_fault=fault;
    if(fault==POWER_RELEASE_ERROR)s_is_available=false;
    if(fault==NORMAL)s_reenter=true;
    esp_err_t error=ota_start_update(progress);
    if(fault==NORMAL){assert(error==ESP_OK && s_written==8205 && s_writes==3 && s_yields==3 && s_removes==1 && s_restarts==1);assert(ota_get_state()==OTA_STATE_REBOOTING);}
    else {assert(error!=ESP_OK && s_restarts==0 && s_removes==0 && ota_get_state()==OTA_STATE_ERROR);}
    if(fault==SHORT_READ || fault==READ_ERROR)assert(s_writes==0 && s_aborts==1);
    if(fault==NO_MEMORY)assert(s_begins==0);
    if(fault==ABORT_ERROR){assert(s_handle_live && s_has_pending_handle && s_has_power_hold && !s_buffer && !s_file_open);s_fault=NORMAL;s_is_available=false;assert(ota_start_update(NULL)==ESP_ERR_NOT_FOUND);assert(!s_handle_live && s_power==0);}
    if(fault==POWER_RELEASE_ERROR){assert(s_has_power_hold && s_power==1);s_fault=NORMAL;assert(ota_start_update(NULL)==ESP_ERR_NOT_FOUND);assert(!s_has_power_hold && s_power==0);}
    s_is_available=true;
    assert(!s_handle_live && !s_file_open && !s_buffer);
  }
  reset();s_ota_state=ESP_OTA_IMG_PENDING_VERIFY;s_state_error=ESP_FAIL;
  assert(ota_post_boot_check()==ESP_FAIL && s_syncs==0 && s_confirms==0);
  reset();s_ota_state=ESP_OTA_IMG_PENDING_VERIFY;s_confirm_error=ESP_FAIL;
  assert(ota_post_boot_check()==ESP_FAIL && s_syncs==0 && s_confirms==1);
  reset();s_ota_state=ESP_OTA_IMG_PENDING_VERIFY;s_assets=false;
  assert(ota_post_boot_check()==ESP_FAIL && s_confirms==0 && s_health_waits==0);
  reset();s_ota_state=ESP_OTA_IMG_PENDING_VERIFY;s_renderer=false;
  assert(ota_post_boot_check()==ESP_FAIL && s_confirms==0 && s_syncs==0);
  reset();s_ota_state=ESP_OTA_IMG_PENDING_VERIFY;s_beat=UINT32_MAX-2;
  assert(ota_post_boot_check()==ESP_OK && s_confirms==1 && s_syncs==1);
  reset();s_state_error=ESP_ERR_NOT_FOUND;
  assert(ota_post_boot_check()==ESP_OK && s_confirms==0 && s_syncs==1);
  puts("PASS: P4 OTA preflight, exclusivity, read/write/finalization errors, retained cleanup and truthful rollback confirmation");
  return 0;
}
