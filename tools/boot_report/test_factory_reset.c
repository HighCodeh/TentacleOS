#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 1
#define FLASH_MOUNT "assets"
#define ESP_LOGW(...) fixture_log(__VA_ARGS__)
#define ESP_LOGE(...) fixture_log(__VA_ARGS__)
static void fixture_log(const char *tag,const char *format,...) {(void)tag;(void)format;}
static bool s_sd,s_assets=true;
static int s_mount_error,s_erase_error,s_init_error;
static unsigned s_mounts,s_erases,s_inits;
static const char *esp_err_to_name(int error) {(void)error;return "fixture";}
static bool storage_assets_is_mounted(void) {return s_assets;}
static bool storage_is_mounted(void) {return s_sd;}
static esp_err_t storage_init(void) {++s_mounts;s_sd=s_mount_error==ESP_OK;return s_mount_error;}
static esp_err_t nvs_flash_erase(void) {++s_erases;return s_erase_error;}
static esp_err_t nvs_flash_init(void) {++s_inits;return s_init_error;}
static bool s_fail_directory,s_fail_unlink;
static DIR *fixture_opendir(const char *path) {if(s_fail_directory){errno=EIO;return NULL;}return opendir(path);}
static int fixture_unlink(const char *path) {if(s_fail_unlink){errno=EIO;return -1;}return unlink(path);}
static void vTaskDelay(unsigned ticks) {assert(ticks==1);}
#define opendir fixture_opendir
#define unlink fixture_unlink
/* PATHS */
/* MODULE */
static void create(const char *directory) {
  assert(mkdir(directory,0700)==0);char path[300];snprintf(path,sizeof(path),"%s/test",directory);
  FILE *file=fopen(path,"wb");assert(file);fputs("user",file);fclose(file);
}
static bool exists(const char *path) {return access(path,F_OK)==0;}
int main(void) {
  assert(mkdir("assets",0700)==0 && mkdir("sd",0700)==0);
  create(FLASH_CONFIG_DIR);create(FLASH_STORAGE_DIR);create(TOS_PATH_CONFIG_DIR);
  create("sd/c5");
  for(int i=0;i<SD_USER_DIRS_COUNT;++i)create(SD_USER_DIRS[i]);
  assert(tos_factory_reset_config()==ESP_OK && s_mounts==1 && s_erases==0);
  assert(!exists("assets/config/test") && !exists("sd/config/test") && exists("assets/storage/test"));
  s_erase_error=ESP_FAIL;assert(tos_factory_reset_all()==ESP_FAIL && s_inits==0);
  assert(!exists("assets/storage/test") && exists("sd/c5/test"));
  s_erase_error=ESP_OK;s_init_error=ESP_FAIL;assert(tos_factory_reset_all()==ESP_FAIL);
  s_init_error=ESP_OK;assert(tos_factory_reset_all()==ESP_OK && s_erases==3 && s_inits==2);
  s_sd=false;s_mount_error=ESP_FAIL;assert(tos_factory_reset_config()==ESP_FAIL && s_mounts==2);
  s_sd=true;s_fail_directory=true;assert(tos_factory_reset_config()!=ESP_OK);s_fail_directory=false;
  s_fail_unlink=true;assert(tos_factory_reset_config()!=ESP_OK);s_fail_unlink=false;
  s_sd=true;s_assets=false;assert(tos_factory_reset_config()==ESP_ERR_INVALID_STATE);
  assert(exists("sd/c5/test"));
  puts("PASS: real factory reset mounts SD only on request, propagates mount/NVS errors and preserves assets/directories/C5 firmware");
}
