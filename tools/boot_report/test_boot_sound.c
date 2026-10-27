#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef int esp_err_t;
typedef void *HMP3Decoder;
typedef struct {int samprate,nChans,outputSamps;} MP3FrameInfo;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_ARG 2
#define ESP_ERR_INVALID_STATE 3
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_INTERNAL 4
#define SYS_PRIO_SERVICE_LO 4
#define SYS_CORE_RADIO 0
#define pdPASS 1
#define ERR_MP3_INDATA_UNDERFLOW -1
#define ESP_LOGE(...) fixture_log(__VA_ARGS__)
#define ESP_LOGW(...) fixture_log(__VA_ARGS__)
static void fixture_log(const char *tag,const char *format,...) {(void)tag;(void)format;}
static jmp_buf s_jump;
static unsigned s_allocations,s_attempt,s_fail_at,s_task_fail,s_mode,s_decodes,s_played;
static size_t s_internal=100000,s_largest=50000;
static int64_t s_now;
static bool s_decoder;
static void (*s_worker)(void *);
static void *heap_caps_malloc(size_t size,unsigned caps) {
  assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(++s_attempt==s_fail_at)return NULL;
  void *p=malloc(size);assert(p);++s_allocations;return p;
}
static void fixture_free(void *p) {if(p){assert(s_allocations);--s_allocations;}free(p);}
static size_t heap_caps_get_free_size(unsigned caps) {assert(caps==MALLOC_CAP_INTERNAL);return s_internal;}
static size_t heap_caps_get_largest_free_block(unsigned caps) {assert(caps==MALLOC_CAP_INTERNAL);return s_largest;}
static int xTaskCreatePinnedToCoreWithCaps(void (*worker)(void *),const char *name,unsigned stack,void *arg,unsigned priority,void *handle,unsigned core,unsigned caps) {
  (void)name;assert(stack==8192 && arg==NULL && priority==4 && handle==NULL && core==0 && caps==6);
  s_worker=worker;return s_task_fail?0:pdPASS;
}
static void vTaskDeleteWithCaps(void *task) {assert(task==NULL && !s_decoder && s_allocations==0);longjmp(s_jump,1);}
static void vTaskDelay(unsigned ticks) {assert(ticks==1);}
static int64_t esp_timer_get_time(void) {s_now+=s_mode==8?6000000:1;return s_now;}
static HMP3Decoder MP3InitDecoder(void) {if(s_mode==7)return NULL;s_decoder=true;return (void *)1;}
static void MP3FreeDecoder(HMP3Decoder decoder) {assert(decoder==(void *)1 && s_decoder);s_decoder=false;}
static int MP3FindSyncWord(uint8_t *data,int remaining) {(void)data;assert(remaining>0);return s_mode==9?remaining:0;}
static int MP3Decode(HMP3Decoder decoder,uint8_t **cursor,int *remaining,int16_t *frame,int free_format) {
  assert(decoder && free_format==0);++s_decodes;
  if(s_mode==1)return ERR_MP3_INDATA_UNDERFLOW;
  if(s_mode==2)return 0;
  if(s_mode==3){*remaining+=1;return 0;}
  ++*cursor;--*remaining;
  if(s_mode==10)return -100;
  for(unsigned i=0;i<2304;++i)frame[i]=(int16_t)(i%2?3000:1000);
  return 0;
}
static void MP3GetLastFrameInfo(HMP3Decoder decoder,MP3FrameInfo *out) {
  assert(decoder);*out=(MP3FrameInfo){48000,2,2304};
  if(s_mode==4)out->nChans=3;
  if(s_mode==5)out->outputSamps=2305;
  if(s_mode==6 && s_decodes>1)out->samprate=8000;
}
static esp_err_t audio_i2s_play_pcm(const int16_t *pcm,size_t count,uint32_t rate) {
  assert(pcm[0]==2000 && count==4608 && rate==48000);++s_played;return ESP_FAIL;
}
static const char *esp_err_to_name(esp_err_t result) {(void)result;return "fixture";}
#define free fixture_free
/* MODULE */
#undef free
static void run_worker(const char *path) {
  assert(boot_sound_play(path)==ESP_OK && s_is_running);
  assert(boot_sound_play(path)==ESP_ERR_INVALID_STATE);
  if(setjmp(s_jump)==0)s_worker(NULL);
  assert(!s_is_running && s_allocations==0 && !s_decoder);
}
int main(void) {
  char path[128];assert(getcwd(path,sizeof(path)));strcat(path,"/tone.mp3");
  FILE *file=fopen(path,"wb");assert(file);assert(fwrite("test",1,4,file)==4);fclose(file);
  assert(boot_sound_play(NULL)==ESP_ERR_INVALID_ARG && boot_sound_play("")==ESP_ERR_INVALID_ARG);
  assert(boot_sound_play("relative")==ESP_ERR_INVALID_ARG);
  char long_path[129];memset(long_path,'/',128);long_path[128]=0;
  assert(boot_sound_play(long_path)==ESP_ERR_INVALID_ARG);
  s_internal=0;assert(boot_sound_play(path)==ESP_ERR_NO_MEM && !s_is_running);s_internal=100000;
  s_largest=1;assert(boot_sound_play(path)==ESP_ERR_NO_MEM && !s_is_running);s_largest=50000;
  s_task_fail=1;assert(boot_sound_play(path)==ESP_ERR_NO_MEM && !s_is_running);s_task_fail=0;
  run_worker(path);assert(s_played==1);
  for(unsigned fail=1;fail<=3;++fail){s_attempt=0;s_fail_at=fail;run_worker(path);}
  s_fail_at=0;
  for(unsigned mode=1;mode<=10;++mode){s_mode=mode;s_decodes=0;run_worker(path);assert(s_played==1);}
  s_mode=0;assert(remove(path)==0);run_worker(path);assert(s_played==1);
  file=fopen(path,"wb");assert(file);assert(fseek(file,BOOT_SOUND_FILE_MAX,SEEK_SET)==0);fputc(0,file);fclose(file);
  run_worker(path);assert(s_played==1);assert(remove(path)==0);
  puts("PASS: real bounded boot sound, internal stack policy, allocations, malformed decoder progress/format, timeout and missing/oversized files");
}
