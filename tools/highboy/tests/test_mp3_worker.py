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

"""Exercise production MP3 worker failure cleanup with decoder/I2S fault injection."""
import os,re,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
source=(ROOT/'firmware_p4/components/Applications/ui/screens/audio/mp3_player_ui.c').read_text()
def function(name):
 start=source.index('static void '+name+'('); a=source.index('{',start);end=a+1;depth=1
 while depth:
  depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
constants='\n'.join(re.findall(r'^#define (?:PATH_MAX_LEN|N_BARS|READBUF_SZ|MP3_MAXFRAME|MP3_DEFAULT_RATE_HZ|MP3_SILENCE_SAMPLES)\s+[^\n]+',source,re.M))
state=source[re.search(r'typedef\s+struct\s*\{\s*char\s+path', source).start():source.index('static int s_vol')]
header=r"""
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define MP3_AUDIO_DECODE 1
#define ESP_OK 0
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define pdMS_TO_TICKS(x) (x)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ERR_MP3_INDATA_UNDERFLOW -1
#define ERR_MP3_MAINDATA_UNDERFLOW -2
typedef void *HMP3Decoder;
typedef struct {int samprate,nChans,outputSamps,bitrate;} MP3FrameInfo;
static int mode,allocations,freed,started,stopped,written,deleted,failed,decode_calls;
static void *heap_caps_malloc(size_t n,int caps) {
 assert(caps==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));allocations++;
 return mode==allocations ? NULL : malloc(n);
}
static HMP3Decoder MP3InitDecoder(void) {return mode==4 ? NULL : (void*)1;}
static void MP3FreeDecoder(void *p) {assert(p);freed++;}
static int MP3FindSyncWord(const unsigned char *p,int n) {(void)p;return mode==10 || n<4 ? -1 : 0;}
static int MP3Decode(void *d,unsigned char **p,int *left,short *pcm,int flag) {
 assert(d && !flag && ++decode_calls<1000);
 if(mode==9 || *left<100) return ERR_MP3_INDATA_UNDERFLOW;
 *p+=100;*left-=100;memset(pcm,0,2304*sizeof(*pcm));return 0;
}
static void MP3GetLastFrameInfo(void *d,MP3FrameInfo *f) {
 assert(d);*f=(MP3FrameInfo){mode==7 ? 99999 : 44100,mode==8 ? 4 : 2,2304,192000};
}
static int audio_i2s_stream_start(unsigned rate) {assert(rate==44100);started++;return mode==5 ? 1 : 0;}
static int audio_i2s_stream_write(const short *p,int n) {assert(p && n>0);written++;return mode==6 ? n-1 : n;}
static void audio_i2s_stream_stop(void) {stopped++;}
static void led_signal_error(void) {failed++;}
static void vTaskDelete(void *p) {assert(!p);deleted++;}
static void vTaskDelay(int ms) {assert(ms>0);}
static void exit_cb(void *p) {(void)p;}
static void ui_async_call(void (*f)(void*),void *p) {f(p);}
"""
tests=r"""
int main(int argc,char **argv) {
 assert(argc==2);
 for(mode=0;mode<=11;mode++) {
  allocations=freed=started=stopped=written=deleted=failed=decode_calls=0;
  s_task_run=true;s_stop_req=mode==11;s_playing=true;s_err=false;s_finished=false;s_exit_req=false;
  mp3_job_t *job=calloc(1,sizeof(*job));assert(job);snprintf(job->path,sizeof(job->path),"%s",argv[1]);
  player_task(job);
  assert(!s_task_run && !s_playing && deleted==1);
  if(mode==0) assert(s_finished && !s_err && written>0 && started==1 && stopped==1);
  else if(mode==11) assert(!s_finished && !s_err && !started);
  else assert(s_err && !s_finished && failed==1);
  if(mode==5) assert(started==1 && stopped==0 && written==0);
  if(mode==6) assert(started==1 && stopped==1 && written==1);
  assert(freed==(mode==4 ? 0 : 1));
 }
 puts("PASS: production MP3 worker EOF, PSRAM/decoder failures, invalid frames, stalled underflow, stream errors and stop");
}
"""
with tempfile.TemporaryDirectory() as tmp:
 tmp=Path(tmp);c=tmp/'test.c';binary=tmp/'test';data=tmp/'test.mp3';data.write_bytes(bytes(8192))
 c.write_text(source.split('#ifndef')[0]+header+'\n'+constants+'\n'+state+function('compute_bars')+'\n'+function('player_task')+tests)
 subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined',str(c),'-o',str(binary),'-lm'],check=True)
 subprocess.run([str(binary),str(data)],check=True,timeout=10)
