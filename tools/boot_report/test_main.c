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

#include <assert.h>
#include <stdio.h>
#define ESP_OK 0
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
static unsigned s_tracks,s_kernel,s_ota,s_console;
static int s_result;
static void boot_report_track_bootloop(void) {++s_tracks;}
static int kernel_init(void) {assert(s_tracks>s_kernel);++s_kernel;return s_result;}
static int ota_post_boot_check(void) {++s_ota;return ESP_OK;}
static void console_service_start(void) {assert(s_kernel>s_console); if(s_result==ESP_OK)assert(s_ota==1); ++s_console;}
/* MODULE */
int main(void) {
  s_result=-1;app_main();assert(s_tracks==1 && s_kernel==1 && s_ota==0 && s_console==1);
  s_result=ESP_OK;app_main();assert(s_tracks==2 && s_kernel==2 && s_ota==1 && s_console==2);
  puts("PASS: real app entry leaves recovery OTA unconfirmed and runs OTA checks only after normal boot");
}
