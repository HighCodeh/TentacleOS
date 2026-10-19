#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "host_link_local_rf.h"
#include "freertos/semphr.h"
#include "spi_protocol.h"

static fake_sem_t s_semaphores[2];
static unsigned s_semaphore_count;
static pthread_t s_worker;
static bool s_is_started, s_has_work, s_exit;
static pthread_mutex_t s_queue_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_queue_ready=PTHREAD_COND_INITIALIZER;
static atomic_bool s_is_held, s_entered;
static esp_err_t s_schedule_error;
static unsigned s_stops;

static SemaphoreHandle_t semaphore(unsigned count) {
  assert(s_semaphore_count<2);
  fake_sem_t *sem=&s_semaphores[s_semaphore_count++];
  assert(pthread_mutex_init(&sem->lock,NULL)==0);
  assert(pthread_cond_init(&sem->ready,NULL)==0);
  sem->count=count; return sem;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return semaphore(1); }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return semaphore(0); }
int xSemaphoreTake(SemaphoreHandle_t sem,uint32_t timeout) {
  pthread_mutex_lock(&sem->lock);
  while(sem->count==0) {
    if(timeout!=UINT32_MAX) { pthread_mutex_unlock(&sem->lock); return 0; }
    pthread_cond_wait(&sem->ready,&sem->lock);
  }
  --sem->count; pthread_mutex_unlock(&sem->lock); return 1;
}
int xSemaphoreGive(SemaphoreHandle_t sem) {
  pthread_mutex_lock(&sem->lock); assert(sem->count==0); ++sem->count;
  pthread_cond_signal(&sem->ready); pthread_mutex_unlock(&sem->lock); return 1;
}
static void *worker(void *unused) {
  (void)unused;
  for(;;) {
    pthread_mutex_lock(&s_queue_lock);
    while(!s_has_work && !s_exit) pthread_cond_wait(&s_queue_ready,&s_queue_lock);
    if(s_exit) { pthread_mutex_unlock(&s_queue_lock); return NULL; }
    s_has_work=false; pthread_mutex_unlock(&s_queue_lock);
    host_local_rf_process();
  }
}
esp_err_t host_link_start_worker(void) {
  if(!s_is_started) { assert(pthread_create(&s_worker,NULL,worker,NULL)==0); s_is_started=true; }
  return ESP_OK;
}
esp_err_t host_link_schedule_local_rf(void) {
  if(s_schedule_error) return s_schedule_error;
  pthread_mutex_lock(&s_queue_lock); assert(!s_has_work); s_has_work=true;
  pthread_cond_signal(&s_queue_ready); pthread_mutex_unlock(&s_queue_lock); return ESP_OK;
}
void host_rf_stop(void) { assert(pthread_equal(s_worker,pthread_self())); ++s_stops; }
uint8_t host_rf_handle(host_rf_request_t *request) {
  assert(pthread_equal(s_worker,pthread_self()));
  assert(request->command==SPI_ID_RFID_READ && request->length==2);
  assert(request->payload[0]==0xf4 && request->payload[1]==1);
  atomic_store(&s_entered,true);
  struct timespec delay={.tv_nsec=1000000};
  while(atomic_load(&s_is_held)) nanosleep(&delay,NULL);
  request->out[0]=1; request->out[1]=0x42; request->out_length=2;
  return SPI_STATUS_OK;
}
static void *caller(void *unused) {
  (void)unused;
  uint8_t payload[]={0xf4,1}, output[HOST_RF_RESPONSE_MAX]={0};
  host_rf_request_t request={.command=SPI_ID_RFID_READ,.payload=payload,.length=sizeof(payload),
    .out=output,.capacity=sizeof(output)};
  assert(host_local_rf_lock(1));
  assert(host_local_rf_execute(&request)==ESP_OK);
  assert(request.out_length==2 && output[0]==1 && output[1]==0x42);
  host_local_rf_unlock(); return NULL;
}
int main(void) {
  assert(host_local_rf_init()==ESP_OK);
  assert(host_local_rf_init()==ESP_OK && s_semaphore_count==2);
  atomic_store(&s_is_held,true);
  pthread_t app; assert(pthread_create(&app,NULL,caller,NULL)==0);
  struct timespec delay={.tv_nsec=1000000};
  while(!atomic_load(&s_entered)) nanosleep(&delay,NULL);
  assert(!host_local_rf_lock(1)); // A force-kill must not delete the blocked caller.
  atomic_store(&s_is_held,false); assert(pthread_join(app,NULL)==0);
  assert(host_local_rf_lock(1)); host_local_rf_unlock();
  assert(s_stops==1);
  // Queue saturation must not leave a completion or lock for the next call.
  uint8_t payload[]={0xf4,1}, output[HOST_RF_RESPONSE_MAX]={0};
  host_rf_request_t request={.command=SPI_ID_RFID_READ,.payload=payload,.length=sizeof(payload),
    .out=output,.capacity=sizeof(output)};
  assert(host_local_rf_lock(1)); s_schedule_error=ESP_ERR_TIMEOUT;
  assert(host_local_rf_execute(&request)==ESP_ERR_TIMEOUT && request.out_length==0);
  s_schedule_error=ESP_OK;
  assert(host_local_rf_execute(&request)==ESP_OK && request.out_length==2);
  request.length=3;
  assert(host_local_rf_execute(&request)==ESP_ERR_INVALID_ARG);
  host_local_rf_unlock();
  pthread_mutex_lock(&s_queue_lock); s_exit=true;
  pthread_cond_signal(&s_queue_ready); pthread_mutex_unlock(&s_queue_lock);
  assert(pthread_join(s_worker,NULL)==0);
  puts("PASS: stable-worker RF execution, owned copies, force-kill quiescence and queue failure recovery");
}
