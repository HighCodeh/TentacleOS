#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define HOST_LINK_RX_CHUNK 64
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
typedef unsigned TickType_t;
typedef void (*host_link_writer_t)(void);
/* RX_ITEM */
static void writer(void) {}
static void *s_rx_queue=(void *)1;
static rx_item_t s_queued;
static unsigned s_local, s_parsed, s_released;
static uint32_t s_dispatch_generation;
static bool s_queue_full;
static bool host_link_generation_matches(host_link_writer_t output,uint32_t generation) {
  return output==writer && generation==1;
}
static void host_link_session_release_generation(host_link_writer_t output,uint32_t generation) {
  assert(output==writer && generation==1); ++s_released;
}
static void reconcile_session(void) {}
static int xQueueSend(void *queue,const rx_item_t *item,unsigned wait) {
  assert(queue==s_rx_queue && wait==0);
  assert(!item->is_local_rf); // Every wire-originated item, including all-ones payloads.
  s_queued=*item; return s_queue_full ? 0 : pdTRUE;
}
static int xQueueReceive(void *queue,rx_item_t *item,unsigned wait) {
  (void)wait; assert(queue==s_rx_queue); *item=s_queued; return pdTRUE;
}
static void host_local_rf_process(void) { assert(s_dispatch_generation==0); ++s_local; }
static void parse_bytes(const uint8_t *bytes,size_t length) {
  assert(bytes!=NULL && length>0 && s_dispatch_generation==1); ++s_parsed;
}
/* FEED */
/* PUMP */
int main(void) {
  uint8_t wire[129]; memset(wire,0xff,sizeof(wire));
  assert(host_link_feed(writer,1,wire,sizeof(wire)));
  assert(!s_queued.is_local_rf && s_queued.len==1);
  assert(pump(0) && s_parsed==1 && s_local==0 && s_dispatch_generation==0);
  s_queued=(rx_item_t){.is_local_rf=true};
  assert(pump(0) && s_local==1 && s_parsed==1);
  s_queued=(rx_item_t){.writer=writer,.generation=2,.len=1};
  assert(pump(0) && s_local==1 && s_parsed==1);
  s_queue_full=true;
  assert(!host_link_feed(writer,1,wire,1) && s_released==1);
  puts("PASS: actual host core separates trusted local RF work from authenticated wire ingress");
}
