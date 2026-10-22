#include <assert.h>
#include <stdio.h>
#define ESP_OK 0
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
static unsigned s_tracks,s_kernel,s_ota;
static int s_result;
static void boot_report_track_bootloop(void) {++s_tracks;}
static int kernel_init(void) {assert(s_tracks>s_kernel);++s_kernel;return s_result;}
static int ota_post_boot_check(void) {++s_ota;return ESP_OK;}
/* MODULE */
int main(void) {
  s_result=-1;app_main();assert(s_tracks==1 && s_kernel==1 && s_ota==0);
  s_result=ESP_OK;app_main();assert(s_tracks==2 && s_kernel==2 && s_ota==1);
  puts("PASS: real app entry leaves recovery OTA unconfirmed and runs OTA checks only after normal boot");
}
