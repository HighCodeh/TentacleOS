#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gatt_explorer.h"
#include "spi_bridge.h"
static int s_failure;
static uint16_t s_last_id;
esp_err_t spi_bridge_send_command(spi_id_t id,const uint8_t *payload,uint8_t length,
    spi_header_t *header,uint8_t *out,size_t capacity,uint32_t timeout) {
  assert(capacity==SPI_MAX_PAYLOAD && timeout==10000);s_last_id=id;
  if(s_failure==1) return ESP_ERR_TIMEOUT;
  out[0]=s_failure==2?SPI_STATUS_ERROR:SPI_STATUS_OK;
  header->length=1;
  if(id==SPI_ID_BT_APP_GATT_EXP) {assert(length==7 && payload[0]==1 && payload[6]==1);}
  else if(id==SPI_ID_BT_GATT_READ) {assert(length==2 && payload[0]==3 && payload[1]==0);}
  else if(id==SPI_ID_BT_GATT_STOP) {assert(!payload && !length);}
  else if(id==SPI_ID_BT_GATT_STATUS) {
    spi_gatt_status_t status={.state=SPI_GATT_READY,.count=2,.value_length=1,.value={42}};
    if(s_failure==4)status.count=SPI_GATT_RECORD_MAX+1;
    if(s_failure==5)status.state=SPI_GATT_STATE_COUNT;
    if(s_failure==6)status.value_length=SPI_GATT_VALUE_MAX+1;
    memcpy(out+1,&status,sizeof(status));header->length+=sizeof(status);
  } else if(id==SPI_ID_BT_GATT_RECORD) {
    assert(length==1 && payload[0]==1);spi_gatt_record_t record={.kind=SPI_GATT_CHARACTERISTIC,.value_handle=3};
    memcpy(out+1,&record,sizeof(record));header->length+=sizeof(record);
  } else assert(0);
  if(s_failure==3)header->length=0;
  return ESP_OK;
}
int main(void) {
 uint8_t address[6]={1};spi_gatt_status_t status;spi_gatt_record_t record;
 assert(!gatt_explorer_start(NULL,1) && !gatt_explorer_start(address,4));
 assert(gatt_explorer_start(address,1) && s_last_id==SPI_ID_BT_APP_GATT_EXP);
 assert(gatt_explorer_get_status(NULL)==ESP_ERR_INVALID_ARG);
 assert(gatt_explorer_get_status(&status)==ESP_OK && status.value[0]==42 && gatt_explorer_is_busy());
 assert(gatt_explorer_get_record(1,&record)==ESP_OK && record.value_handle==3);
 assert(gatt_explorer_get_record(SPI_GATT_RECORD_MAX,&record)==ESP_ERR_INVALID_ARG);
 assert(gatt_explorer_read(3)==ESP_OK && gatt_explorer_stop()==ESP_OK);
 for(s_failure=1;s_failure<=6;++s_failure)assert(gatt_explorer_get_status(&status)!=ESP_OK);
 s_failure=2;assert(!gatt_explorer_start(address,1));
 s_failure=3;assert(gatt_explorer_get_record(1,&record)!=ESP_OK);
 puts("PASS: production P4 GATT proxy command sizes, transport/status errors and response bounds");
 return 0;
}
