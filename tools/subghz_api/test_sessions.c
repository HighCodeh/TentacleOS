static pthread_t s_thread;
static pthread_mutex_t s_notify_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_notify_condition=PTHREAD_COND_INITIALIZER;
static unsigned s_notifications;
static bool s_fail_task;
static void *run_session_thread(void *unused) {
  (void)unused;s_task=3;session_task(NULL);return NULL;
}
BaseType_t xTaskCreatePinnedToCore(void (*task)(void *),const char *name,uint32_t stack,void *arg,
                                   int priority,TaskHandle_t *out,int core) {
  assert(task==session_task && strcmp(name,"subghz_app")==0 && stack==4096 && arg==NULL);
  assert(priority==SYS_PRIO_SERVICE_LO && core==SYS_CORE_RADIO);
  if(s_fail_task)return 0;
  assert(pthread_create(&s_thread,NULL,run_session_thread,NULL)==0);
  *out=(void *)3;return pdPASS;
}
static void unlock_notification(void *mutex) {assert(pthread_mutex_unlock(mutex)==0);}
uint32_t ulTaskNotifyTake(BaseType_t clear,TickType_t timeout) {
  assert(s_task==3 && clear==pdTRUE && timeout==portMAX_DELAY);
  assert(pthread_mutex_lock(&s_notify_lock)==0);
  unsigned count=0;
  pthread_cleanup_push(unlock_notification,&s_notify_lock);
  while(s_notifications==0)assert(pthread_cond_wait(&s_notify_condition,&s_notify_lock)==0);
  count=s_notifications;s_notifications=0;
  pthread_cleanup_pop(1);
  return count;
}
void xTaskNotifyGive(TaskHandle_t task) {
  assert(task==(void *)3);assert(pthread_mutex_lock(&s_notify_lock)==0);
  ++s_notifications;assert(pthread_cond_signal(&s_notify_condition)==0);
  assert(pthread_mutex_unlock(&s_notify_lock)==0);
}
static void wait_phase(tos_api_subghz_phase_t phase) {
  for(unsigned i=0;i<1000;++i) {
    portENTER_CRITICAL(&s_session_lock);bool ready=s_session.phase==phase;
    portEXIT_CRITICAL(&s_session_lock);
    if(ready)return;
    vTaskDelay(SUBGHZ_SESSION_RETRY_MS);
  }
  assert(!"session phase timed out");
}
static int poll_session(tos_subghz_signal_t *out) {
  bool owned=false;int count=tos_api_subghz_session_poll(out,&owned);assert(owned);return count;
}
int main(void) {
  uint32_t id=99;tos_subghz_signal_t signal={0};bool owned=true;
  assert(tos_api_subghz_session_poll(&signal,&owned)==0 && !owned);
  assert(tos_api_subghz_rx_start(350000000,6,&id)==ESP_ERR_INVALID_ARG && id==0);
  assert(tos_api_subghz_rx_start(433920000,0,&id)==ESP_ERR_INVALID_ARG && id==0);
  assert(tos_api_subghz_rx_start(433920000,6,NULL)==ESP_ERR_INVALID_ARG);
  assert(tos_api_subghz_spectrum_start(300000000,1000000,&id)==ESP_ERR_INVALID_ARG);
  s_allowed=false;assert(tos_api_subghz_rx_start(433920000,6,&id)==ESP_ERR_NOT_SUPPORTED);
  s_allowed=true;s_fail_task=true;
  assert(tos_api_subghz_rx_start(433920000,6,&id)==ESP_ERR_NO_MEM && id==0);clean();
  s_fail_task=false;s_resource_busy=true;
  assert(tos_api_subghz_rx_start(433920000,6,&id)==ESP_ERR_INVALID_STATE && id==0);
  wait_phase(TOS_SUBGHZ_SESSION_IDLE);clean();s_resource_busy=false;
  s_fault=RX_TIMEOUT;
  assert(tos_api_subghz_rx_start(433920000,6,&id)==ESP_OK && id!=0);
  assert(poll_session(&signal)==0);
  uint32_t other=0;assert(tos_api_subghz_rx_start(433920000,6,&other)==ESP_ERR_INVALID_STATE);
  assert(tos_api_subghz_receive(&signal,433920000,6,100)==ESP_ERR_INVALID_STATE);
  int32_t timings[]={350,-700,350};
  assert(tos_api_subghz_tx_raw(timings,3,433920000)==ESP_ERR_INVALID_STATE);
  s_fault=NORMAL;
  for(unsigned i=0;i<1000;++i) {
    if(poll_session(&signal)==1)break;
    vTaskDelay(SUBGHZ_SESSION_RETRY_MS);
  }
  assert(signal.freq_hz==433920000 && signal.value==42);
  int32_t raw[6];assert(tos_api_subghz_get_raw(raw,6)==6 && raw[0]==350 && raw[1]==-700);
  s_task=4;
  assert(tos_api_subghz_session_stop(id)==ESP_ERR_INVALID_STATE);
  assert(tos_api_subghz_set_frequency(315000000)==ESP_ERR_INVALID_STATE);
  assert(tos_api_subghz_session_poll(&signal,&owned)==0 && !owned);s_task=1;
  assert(tos_api_subghz_set_frequency(315000000)==ESP_OK);
  for(unsigned i=0;i<1000;++i) {
    if(poll_session(&signal)==1 && signal.freq_hz==315000000)break;
    vTaskDelay(SUBGHZ_SESSION_RETRY_MS);
  }
  assert(signal.freq_hz==315000000);
  for(unsigned i=0;i<1000;++i) {
    portENTER_CRITICAL(&s_session_lock);bool pending=s_session.has_new;
    portEXIT_CRITICAL(&s_session_lock);
    if(pending)break;
    vTaskDelay(SUBGHZ_SESSION_RETRY_MS);
  }
  s_fault=RX_TIMEOUT;
  for(unsigned i=0;i<10;++i)vTaskDelay(SUBGHZ_SESSION_RETRY_MS);
  assert(poll_session(&signal)==1 && signal.value==42 && signal.freq_hz==315000000);
  assert(poll_session(&signal)==0);
  s_allowed=false;
  assert(poll_session(&signal)==-ESP_ERR_NOT_SUPPORTED);
  assert(tos_api_subghz_session_stop(id)==ESP_ERR_NOT_SUPPORTED);
  s_allowed=true;
  s_stopped=true;assert(tos_api_subghz_session_stop(id)==ESP_OK);s_stopped=false;clean();
  assert(tos_api_subghz_session_stop(id)==ESP_OK);
  assert(tos_api_subghz_session_stop(0)==ESP_ERR_INVALID_STATE);
  assert(tos_api_subghz_session_stop(id-1)==ESP_ERR_INVALID_STATE);
  // Hardware timeouts must not be mistaken for an ordinary empty RX window.
  s_fault=FAIL_PRESET;
  assert(tos_api_subghz_rx_start(433920000,6,&other)==ESP_ERR_TIMEOUT && other==0);
  wait_phase(TOS_SUBGHZ_SESSION_IDLE);clean();s_fault=NORMAL;
  assert(tos_api_subghz_rx_start(433920000,6,&id)==ESP_OK);
  s_fault=FAIL_DISABLE;wait_phase(TOS_SUBGHZ_SESSION_STOPPING);
  assert(poll_session(&signal)<0 && s_lost_events!=0);
  assert(tos_api_subghz_session_stop(id)==ESP_ERR_TIMEOUT);
  assert(!cc1101_lease_enter());
  s_fault=NORMAL;wait_phase(TOS_SUBGHZ_SESSION_IDLE);
  assert(tos_api_subghz_session_stop(id)==ESP_OK);clean();
  assert(tos_api_subghz_rx_start(433920000,6,&id)==ESP_OK);
  tos_api_subghz_cancel_owner((void *)4);assert(poll_session(&signal)>=0);
  tos_api_subghz_cancel_owner((void *)1);wait_phase(TOS_SUBGHZ_SESSION_IDLE);clean();
  assert(tos_api_subghz_session_poll(&signal,&owned)==0 && !owned);
  s_task=4;assert(tos_api_subghz_rx_start(433920000,6,&other)==ESP_OK);
  s_task=1;assert(tos_api_subghz_session_stop(other)==ESP_ERR_INVALID_STATE);
  s_task=4;assert(tos_api_subghz_session_stop(other)==ESP_OK);s_task=1;clean();
  s_pause_session=true;
  assert(tos_api_subghz_rx_start(433920000,6,&id)==ESP_ERR_TIMEOUT && id==0);
  tos_api_subghz_cancel_owner((void *)1);s_pause_session=false;
  wait_phase(TOS_SUBGHZ_SESSION_IDLE);clean();
  assert(tos_api_subghz_spectrum_start(433920000,1000000,&id)==ESP_OK);
  assert(tos_api_subghz_session_stop(other)==ESP_ERR_INVALID_STATE);
  assert(poll_session(&signal)==-ESP_ERR_INVALID_STATE);
  float bins[80];uint32_t start=123,step=456;
  for(unsigned i=0;i<80;++i)bins[i]=99;
  assert(tos_api_subghz_spectrum_poll(&start,&step,bins,79)==-ESP_ERR_INVALID_SIZE);
  assert(start==123 && step==456 && bins[0]==99);
  int count=0;
  for(unsigned i=0;i<1000;++i) {
    count=tos_api_subghz_spectrum_poll(&start,&step,bins,80);
    if(count==80)break;
    vTaskDelay(SUBGHZ_SESSION_RETRY_MS);
  }
  assert(count==80 && start==433420000 && step==12500 && bins[79]==-68.0f);
  assert(tos_api_subghz_set_frequency(315000000)==ESP_ERR_INVALID_STATE);
  s_fault=FAIL_TUNE;wait_phase(TOS_SUBGHZ_SESSION_IDLE);
  start=123;step=456;bins[0]=99;
  assert(tos_api_subghz_spectrum_poll(&start,&step,bins,80)<0);
  assert(start==123 && step==456 && bins[0]==99);
  s_fault=NORMAL;assert(tos_api_subghz_session_stop(id)==ESP_OK);clean();
  s_session_sequence=UINT32_MAX;
  assert(tos_api_subghz_rx_start(433920000,6,&other)==ESP_ERR_INVALID_STATE && other==0);
  assert(s_clear_events>=4);
  assert(pthread_cancel(s_thread)==0);assert(pthread_join(s_thread,NULL)==0);
  free(s_last_raw);s_last_raw=NULL;s_last_raw_count=0;
  puts("PASS: real asynchronous Sub-GHz worker, RX/spectrum start, retune, copied polls, task ownership, binding cancellation, startup failures and retained hardware cleanup");
}
