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
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#define MAX_KEY_SIZE 32
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG -2
#define ESP_LOGE(...) ((void)0)
typedef int esp_err_t;
static unsigned writes,closes,fail_write;
static bool fail_open,fail_close,stream_error;
static char contents[256];
static FILE *open_file(const char *path,const char *mode) {
 assert(path && strcmp(mode,"a")==0);return fail_open?NULL:(FILE *)&contents;
}
static int put_character(int c,FILE *f) {
 (void)f;if(++writes==fail_write){stream_error=true;return EOF;}
 size_t n=strlen(contents);contents[n]=(char)c;contents[n+1]=0;return c;
}
static int print_bytes(FILE *f,const char *format,...) {
 (void)f;if(++writes==fail_write){stream_error=true;return -1;}
 va_list args;va_start(args,format);int n=vsnprintf(contents+strlen(contents),sizeof(contents)-strlen(contents),format,args);va_end(args);return n;
}
static int close_file(FILE *f) {(void)f;closes++;return fail_close?EOF:0;}
static int file_error(FILE *f) {(void)f;return stream_error;}
#define fopen open_file
#define fputc put_character
#define fprintf print_bytes
#define ferror file_error
#define fclose close_file
/* PRODUCTION */
static void reset(void) {writes=closes=fail_write=0;fail_open=fail_close=stream_error=false;contents[0]=0;}
int main(void) {
 uint8_t key[6]={1,2,3,4,5,6};reset();
 assert(nfc_dict_append_key(NULL,key,6)==ESP_ERR_INVALID_ARG);
 assert(nfc_dict_append_key("x",key,33)==ESP_ERR_INVALID_ARG);
 fail_open=true;assert(nfc_dict_append_key("x",key,6)==ESP_FAIL && closes==0);
 for(unsigned fault=1;fault<=8;fault++) {
  reset();fail_write=fault;assert(nfc_dict_append_key("x",key,6)==ESP_FAIL && closes==1);
 }
 reset();fail_close=true;assert(nfc_dict_append_key("x",key,6)==ESP_FAIL && closes==1);
 reset();strcpy(contents,"AABB");assert(nfc_dict_append_key("x",key,6)==ESP_OK);
 assert(strcmp(contents,"AABB\n010203040506\n")==0 && closes==1);
 puts("PASS: real dictionary append propagates open, every write and delayed close failure; interrupted lines cannot merge with a retried key");
}
