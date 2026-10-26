// SPDX-License-Identifier: GPL-3.0-or-later
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
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "freertos/task.h"
#include "lora_session.h"
#include "firmware_crypto.h"
#include "meshtastic_channels.h"
#include "meshtastic_mqtt.h"
#include "meshtastic_mqtt_codec.h"
#include "meshtastic_regions.h"
#include "mqtt_bridge.h"
#include "mqtt_client.h"
#include "spi_bridge.h"
#include "sys_prio.h"

static pthread_mutex_t s_fake_lock;
static pthread_mutex_t s_notify_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_notify=PTHREAD_COND_INITIALIZER;
static pthread_t s_thread;
static bool s_has_notification;
static atomic_bool s_exit, s_auto_broker;
static bool s_allocated, s_running, s_delivered;
static int s_failure;
static unsigned s_publishes, s_stops, s_chat_messages;
static char s_chat[MT_MQTT_TEXT_MAX], s_who[32];
static char s_uri[128], s_user[64], s_password[64], s_subscribe[96], s_topic[96];
static uint8_t s_published[512];
static int s_published_length;
static void (*s_callback)(void *,esp_event_base_t,int32_t,void *);
static atomic_int s_fail_stop, s_lose_start;
static lora_proto_t s_proto=LORA_PROTO_MESHTASTIC;
static const mt_channel_t s_channel={.is_used=true,.role=MT_CH_PRIMARY,.hash=0x7f,
  .name="LongFast",.psk={1,2,3,4}};
static const mt_region_info_t s_region={.name="US"};
#define HANDLE ((void *)1)
#define LOCK() pthread_mutex_lock(&s_fake_lock)
#define UNLOCK() pthread_mutex_unlock(&s_fake_lock)

static void pause_ms(void) { struct timespec delay={.tv_nsec=1000000}; nanosleep(&delay,NULL); }
static void event(int type,int id,char *data,int length,int total,int offset) {
  esp_mqtt_event_t message={.client=HANDLE,.msg_id=id,.data=data,.data_len=length,
    .total_data_len=total,.current_data_offset=offset};
  assert(s_callback); s_callback(NULL,NULL,type,&message);
}
esp_err_t esp_crt_bundle_attach(void *config) { (void)config; return ESP_OK; }
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config) {
  LOCK();
  assert(!s_allocated);
  if(s_failure==1) { UNLOCK(); return NULL; }
  assert(config->task.priority==SYS_PRIO_SERVICE_LO && config->task.stack_size>=6144);
  assert(config->broker.verification.crt_bundle_attach==esp_crt_bundle_attach);
  snprintf(s_uri,sizeof(s_uri),"%s",config->broker.address.uri);
  snprintf(s_user,sizeof(s_user),"%s",config->credentials.username ? config->credentials.username : "");
  snprintf(s_password,sizeof(s_password),"%s",config->credentials.authentication.password ? config->credentials.authentication.password : "");
  s_allocated=true; s_delivered=false; UNLOCK(); return HANDLE;
}
esp_err_t esp_mqtt_client_register_event(void *client,int id,
    void (*callback)(void *,esp_event_base_t,int32_t,void *),void *arg) {
  (void)arg; assert(client==HANDLE && id==MQTT_EVENT_ANY); LOCK();
  s_callback=callback; int result=s_failure==2 ? ESP_FAIL : ESP_OK; UNLOCK(); return result;
}
esp_err_t esp_mqtt_client_start(void *client) {
  assert(client==HANDLE); LOCK(); int result=s_failure==3 ? ESP_FAIL : ESP_OK;
  if(result==ESP_OK) {
    s_running=true;
  }
  UNLOCK();
  return result;
}
esp_err_t esp_mqtt_client_stop(void *client) {
  assert(client==HANDLE); LOCK(); assert(s_allocated && s_running);
  s_running=false; ++s_stops; event(MQTT_EVENT_DISCONNECTED,0,NULL,0,0,0); UNLOCK(); return ESP_OK;
}
esp_err_t esp_mqtt_client_destroy(void *client) {
  assert(client==HANDLE); LOCK(); assert(s_allocated && !s_running);
  s_allocated=false; s_callback=NULL; UNLOCK(); return ESP_OK;
}
int esp_mqtt_client_subscribe(void *client,const char *topic,int qos) {
  assert(client==HANDLE && qos==0); LOCK(); snprintf(s_subscribe,sizeof(s_subscribe),"%s",topic);
  UNLOCK(); return 7;
}
int esp_mqtt_client_publish(void *client,const char *topic,const char *data,int length,int qos,int retain) {
  assert(client==HANDLE && qos==0 && retain==0 && length>0 && length<=512); LOCK();
  assert(s_running); memcpy(s_published,data,length); s_published_length=length;
  snprintf(s_topic,sizeof(s_topic),"%s",topic); ++s_publishes; UNLOCK(); return 11;
}

static spi_status_t execute(uint16_t command,const uint8_t *payload,uint8_t length,uint8_t *out,uint8_t *out_length) {
  mqtt_bridge_request_t request={.command=command,.payload=payload,.length=length,.out=out,.out_length=out_length};
  return mqtt_bridge_execute(&request);
}
static spi_status_t field(uint8_t id,const char *value) {
  uint8_t payload[255],out[255],length;
  payload[0]=id; memcpy(payload+1,value,strlen(value));
  return execute(SPI_ID_MQTT_CONFIG,payload,strlen(value)+1,out,&length);
}
static void configure(void) {
  assert(field(SPI_MQTT_FIELD_URI,"mqtts://example.invalid")==SPI_STATUS_OK);
  assert(field(SPI_MQTT_FIELD_USERNAME,"test")==SPI_STATUS_OK);
  assert(field(SPI_MQTT_FIELD_PASSWORD,"secret")==SPI_STATUS_OK);
  assert(field(SPI_MQTT_FIELD_SUBSCRIBE,"msh/US/2/e/LongFast/#")==SPI_STATUS_OK);
  assert(field(SPI_MQTT_FIELD_PUBLISH,"msh/US/2/e/LongFast/!22334455")==SPI_STATUS_OK);
}
static uint32_t u32(const uint8_t *p) { return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void put32(uint8_t *p,uint32_t value) { for(unsigned i=0;i<4;++i) p[i]=(uint8_t)(value>>(8*i)); }
static void put16(uint8_t *p,uint16_t value) { p[0]=(uint8_t)value; p[1]=(uint8_t)(value>>8); }
static void subscribe(void) {
  char granted=0;
  event(MQTT_EVENT_CONNECTED,0,NULL,0,0,0);
  event(MQTT_EVENT_SUBSCRIBED,7,&granted,1,1,0);
}
static void test_bridge(void) {
  uint8_t out[255],length;
  assert(execute(SPI_ID_MQTT_START,NULL,0,out,&length)==SPI_STATUS_INVALID_ARG);
  configure();
  for(s_failure=1;s_failure<=3;++s_failure) {
    assert(execute(SPI_ID_MQTT_START,NULL,0,out,&length)==SPI_STATUS_ERROR);
    assert(!s_allocated && !s_running);
  }
  s_failure=0;
  assert(field(SPI_MQTT_FIELD_PUBLISH,"bad/#")==SPI_STATUS_INVALID_ARG);
  assert(execute(SPI_ID_MQTT_START,NULL,0,out,&length)==SPI_STATUS_OK);
  assert(field(SPI_MQTT_FIELD_USERNAME,"other")==SPI_STATUS_BUSY);
  assert(execute(SPI_ID_MQTT_STATUS,NULL,0,out,&length)==SPI_STATUS_OK && length==13 && out[0]==1);
  event(MQTT_EVENT_CONNECTED,0,NULL,0,0,0);
  char granted=0,denied=(char)0x80;
  event(MQTT_EVENT_SUBSCRIBED,8,&granted,1,1,0);
  execute(SPI_ID_MQTT_STATUS,NULL,0,out,&length); assert(!(out[0]&SPI_MQTT_FLAG_SUBSCRIBED));
  event(MQTT_EVENT_SUBSCRIBED,7,&denied,1,1,0);
  execute(SPI_ID_MQTT_STATUS,NULL,0,out,&length); assert(!(out[0]&SPI_MQTT_FLAG_SUBSCRIBED) && (out[0]&SPI_MQTT_FLAG_ERROR));
  event(MQTT_EVENT_SUBSCRIBED,7,&granted,1,1,0);
  execute(SPI_ID_MQTT_STATUS,NULL,0,out,&length); assert((out[0]&SPI_MQTT_FLAG_SUBSCRIBED) && !(out[0]&SPI_MQTT_FLAG_ERROR));
  event(MQTT_EVENT_DISCONNECTED,0,NULL,0,0,0);
  execute(SPI_ID_MQTT_STATUS,NULL,0,out,&length);
  assert((out[0]&SPI_MQTT_FLAG_ERROR) && !(out[0]&SPI_MQTT_FLAG_CONNECTED));
  subscribe();
  uint8_t bytes[300]; for(unsigned i=0;i<sizeof(bytes);++i) bytes[i]=(uint8_t)i;
  event(MQTT_EVENT_DATA,0,(char *)bytes,100,300,0);
  event(MQTT_EVENT_DATA,0,(char *)bytes+100,200,300,100);
  uint8_t poll[6]={0};
  assert(execute(SPI_ID_MQTT_RECEIVE,poll,sizeof(poll),out,&length)==SPI_STATUS_OK && length==254);
  uint32_t sequence=u32(out); assert(sequence!=0 && memcmp(out+8,bytes,246)==0);
  put32(poll,sequence); put16(poll+4,246);
  assert(execute(SPI_ID_MQTT_RECEIVE,poll,sizeof(poll),out,&length)==SPI_STATUS_OK && length==62);
  assert(memcmp(out+8,bytes+246,54)==0);
  uint8_t ack[4]; put32(ack,sequence+1);
  assert(execute(SPI_ID_MQTT_ACK,ack,4,out,&length)==SPI_STATUS_INVALID_ARG);
  put32(ack,sequence); assert(execute(SPI_ID_MQTT_ACK,ack,4,out,&length)==SPI_STATUS_OK);
  memset(poll,0,sizeof(poll));
  assert(execute(SPI_ID_MQTT_RECEIVE,poll,6,out,&length)==SPI_STATUS_OK && length==0);
  uint8_t chunk[255]; put32(chunk,99); put16(chunk+4,0); put16(chunk+6,300);
  memcpy(chunk+8,bytes,246);
  assert(execute(SPI_ID_MQTT_PUBLISH,chunk,254,out,&length)==SPI_STATUS_OK);
  assert(execute(SPI_ID_MQTT_PUBLISH,chunk,254,out,&length)==SPI_STATUS_OK && s_publishes==0);
  put16(chunk+4,246); memcpy(chunk+8,bytes+246,54);
  assert(execute(SPI_ID_MQTT_PUBLISH,chunk,62,out,&length)==SPI_STATUS_OK && s_publishes==1);
  assert(execute(SPI_ID_MQTT_PUBLISH,chunk,62,out,&length)==SPI_STATUS_OK && s_publishes==1);
  assert(s_published_length==300 && memcmp(s_published,bytes,300)==0);
  put32(chunk,100); put16(chunk+4,1);
  assert(execute(SPI_ID_MQTT_PUBLISH,chunk,62,out,&length)==SPI_STATUS_INVALID_ARG);
  for(unsigned i=0;i<5;++i) event(MQTT_EVENT_DATA,0,(char *)bytes,10,10,0);
  execute(SPI_ID_MQTT_STATUS,NULL,0,out,&length); assert(u32(out+9)==1);
  event(MQTT_EVENT_DATA,0,(char *)bytes,3,6,0);
  event(MQTT_EVENT_DATA,0,(char *)bytes,2,6,4);
  execute(SPI_ID_MQTT_STATUS,NULL,0,out,&length); assert(u32(out+9)==2);
  assert(execute(SPI_ID_MQTT_STOP,NULL,0,out,&length)==SPI_STATUS_OK);
  assert(execute(SPI_ID_MQTT_STOP,NULL,0,out,&length)==SPI_STATUS_OK && !s_allocated);
  puts("PASS: real C5 bridge allocation/start failures, SUBACK, fragmentation, idempotent publish, ACK and overflow");
}

static void *task_entry(void *arg) { void (**entry)(void *)=arg; (*entry)(NULL); return NULL; }
static void (*s_task_fn)(void *);
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,uint32_t stack,void *arg,
    unsigned priority,TaskHandle_t *out,int core) {
  (void)name; assert(arg==NULL && stack>=8192 && priority==SYS_PRIO_SERVICE_LO && core==SYS_CORE_RADIO);
  s_task_fn=fn; *out=&s_thread; assert(pthread_create(&s_thread,NULL,task_entry,&s_task_fn)==0); return pdPASS;
}
void xTaskNotifyGive(void *task) {
  assert(task==&s_thread); pthread_mutex_lock(&s_notify_lock); s_has_notification=true;
  pthread_cond_signal(&s_notify); pthread_mutex_unlock(&s_notify_lock);
}
uint32_t ulTaskNotifyTake(BaseType_t clear,TickType_t timeout) {
  (void)clear;
  pthread_mutex_lock(&s_notify_lock);
  if(!s_has_notification && !atomic_load(&s_exit)) {
    if(timeout==portMAX_DELAY) pthread_cond_wait(&s_notify,&s_notify_lock);
    else {
      struct timespec deadline; clock_gettime(CLOCK_REALTIME,&deadline); deadline.tv_nsec+=5000000;
      if(deadline.tv_nsec>=1000000000) { ++deadline.tv_sec; deadline.tv_nsec-=1000000000; }
      (void)pthread_cond_timedwait(&s_notify,&s_notify_lock,&deadline);
    }
  }
  s_has_notification=false; pthread_mutex_unlock(&s_notify_lock);
  if(atomic_load(&s_exit)) pthread_exit(NULL);
  return 1;
}
lora_proto_t lora_session_active(void) { return s_proto; }
const mt_channel_t *mt_channel_get(uint8_t index) { return index==0 ? &s_channel : NULL; }
mt_region_t mt_region_current(void) { return MT_REGION_US; }
const mt_region_info_t *mt_region_info(mt_region_t id) { assert(id==MT_REGION_US); return &s_region; }
esp_err_t esp_efuse_mac_get_default(uint8_t *mac) {
  const uint8_t value[]={0,0x11,0x22,0x33,0x44,0x55}; memcpy(mac,value,sizeof(value)); return ESP_OK;
}
void lora_session_on_rx_text(const char *who,const char *text) {
  LOCK(); snprintf(s_who,sizeof(s_who),"%s",who); snprintf(s_chat,sizeof(s_chat),"%s",text);
  ++s_chat_messages; UNLOCK();
}
esp_err_t firmware_crypto_cipher(const firmware_crypto_cipher_t *params,
                                firmware_crypto_span_t input, uint8_t *out) {
  const uint8_t *nonce=params->iv.data;
  assert(params->algorithm==PSA_ALG_CTR && params->key.size==16 && params->iv.size==16);
  assert(params->key.data[0]==1 && input.size<=239);
  assert(u32(nonce)==1 && u32(nonce+4)==0 && u32(nonce+8)==0x12345678 && u32(nonce+12)==0);
  memcpy(out,input.data,input.size); return ESP_OK; // Explicit fixture, not cryptographic validation.
}
esp_err_t spi_bridge_send_command(spi_id_t id,const uint8_t *payload,uint8_t length,
    spi_header_t *header,uint8_t *out,size_t capacity,uint32_t timeout) {
  assert(capacity>=255 && timeout==10000); LOCK();
  if(id==SPI_ID_MQTT_STOP && atomic_load(&s_fail_stop)>0) {
    atomic_fetch_sub(&s_fail_stop,1); UNLOCK(); return ESP_ERR_TIMEOUT;
  }
  if(id==SPI_ID_MQTT_STATUS && atomic_load(&s_auto_broker) && s_running && !s_delivered) {
    subscribe(); s_delivered=true;
  }
  uint8_t reply_length=0;
  spi_status_t status=execute(id,payload,length,out+1,&reply_length);
  out[0]=status; memset(header,0,sizeof(*header)); header->length=reply_length+1;
  if(id==SPI_ID_MQTT_START && atomic_load(&s_lose_start)>0 && status==SPI_STATUS_OK) {
    atomic_fetch_sub(&s_lose_start,1); UNLOCK(); return ESP_ERR_TIMEOUT;
  }
  UNLOCK(); return ESP_OK;
}
static void wait_state(mt_mqtt_state_t state) {
  for(unsigned i=0;i<1000;++i) {
    mt_mqtt_snapshot_t snapshot; meshtastic_mqtt_get_snapshot(&snapshot);
    if(snapshot.state==state) return;
    pause_ms();
  }
  assert(!"Timed out waiting for actual MQTT state");
}
static void test_adapter(void) {
  assert(meshtastic_mqtt_init("bad","","")==ESP_ERR_INVALID_ARG);
  s_proto=LORA_PROTO_NONE;
  assert(meshtastic_mqtt_init("mqtts://example.invalid","","")==ESP_ERR_INVALID_STATE);
  s_proto=LORA_PROTO_MESHTASTIC;
  assert(meshtastic_mqtt_init("mqtts://example.invalid","test","secret")==ESP_OK);
  wait_state(MT_MQTT_CONNECTING); assert(!meshtastic_mqtt_is_connected());
  atomic_store(&s_auto_broker,true); wait_state(MT_MQTT_LINKED);
  assert(meshtastic_mqtt_is_connected()); LOCK();
  assert(strcmp(s_subscribe,"msh/US/2/e/LongFast/#")==0 && strcmp(s_password,"secret")==0);
  unsigned published=s_publishes; UNLOCK();
  uint8_t radio[]={0xff,0xff,0xff,0xff,0x55,0x44,0x33,0x22,99,0,0,0,0,0x7f,0,0,
    0x08,1,0x12,5,'h','e','l','l','o'};
  meshtastic_mqtt_publish_radio(radio,sizeof(radio));
  for(unsigned i=0;i<1000;++i) { LOCK(); bool done=s_publishes>published; UNLOCK(); if(done) break; pause_ms(); }
  LOCK(); assert(s_publishes==published+1);
  assert(strcmp(s_topic,"msh/US/2/e/LongFast/!22334455")==0);
  mt_mqtt_envelope_t envelope; assert(mt_mqtt_decode(s_published,s_published_length,&envelope)==ESP_OK);
  assert(strcmp(envelope.channel,"LongFast")==0 && strcmp(envelope.gateway,"!22334455")==0);
  // Echo from this gateway must not enter chat.
  event(MQTT_EVENT_DATA,0,(char *)s_published,s_published_length,s_published_length,0); UNLOCK();
  for (unsigned i=0;i<20;++i) {
    pause_ms();
  }
  LOCK(); assert(s_chat_messages==0); UNLOCK();
  radio[4]=0x78; radio[5]=0x56; radio[6]=0x34; radio[7]=0x12; radio[8]=1;
  uint8_t packet[512],message[512]; uint16_t packet_length,size;
  assert(mt_mqtt_encode_radio(radio,sizeof(radio),packet,&packet_length)==ESP_OK);
  envelope=(mt_mqtt_envelope_t){.packet=packet,.packet_length=packet_length,
    .channel="LongFast",.gateway="!aabbccdd"};
  assert(mt_mqtt_encode(&envelope,message,sizeof(message),&size)==ESP_OK);
  LOCK(); event(MQTT_EVENT_DATA,0,(char *)message,10,size,0);
  event(MQTT_EVENT_DATA,0,(char *)message+10,size-10,size,10); UNLOCK();
  for(unsigned i=0;i<1000;++i) { LOCK(); bool done=s_chat_messages!=0; UNLOCK(); if(done) break; pause_ms(); }
  LOCK(); assert(s_chat_messages==1 && strcmp(s_chat,"hello")==0 && strcmp(s_who,"mqtt:!12345678")==0);
  event(MQTT_EVENT_DATA,0,(char *)message,size,size,0); UNLOCK();
  for (unsigned i=0;i<20;++i) {
    pause_ms();
  }
  LOCK(); assert(s_chat_messages==1); UNLOCK();
  atomic_store(&s_fail_stop,1); meshtastic_mqtt_stop(); wait_state(MT_MQTT_STOPPED);
  LOCK(); assert(!s_allocated && !s_running); UNLOCK();
  // A lost START response must still cause remote cleanup when cancelled.
  atomic_store(&s_auto_broker,false); atomic_store(&s_lose_start,1);
  assert(meshtastic_mqtt_init("mqtts://example.invalid","test","secret")==ESP_OK);
  wait_state(MT_MQTT_ERROR); meshtastic_mqtt_stop(); wait_state(MT_MQTT_STOPPED);
  LOCK(); assert(!s_allocated && !s_running); UNLOCK();
  atomic_store(&s_exit,true); xTaskNotifyGive(&s_thread); assert(pthread_join(s_thread,NULL)==0);
  puts("PASS: real P4/C5 bridge path, truthful link state, encrypted-envelope routing, echo/dedup and cancellation cleanup");
}
void test_codec(void);
int main(void) {
  pthread_mutexattr_t attr; pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&s_fake_lock,&attr); pthread_mutexattr_destroy(&attr);
  test_codec(); test_bridge(); test_adapter();
}
