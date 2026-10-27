#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "meshtastic_mqtt_codec.h"

void test_codec(void) {
  const uint8_t text[]={0x08,1,0x12,5,'h','e','l','l','o'};
  uint8_t radio[MT_MQTT_RADIO_MAX]={0xff,0xff,0xff,0xff,0x78,0x56,0x34,0x12,1,0,0,0,0,0x7f,0,0};
  memcpy(radio+16,text,sizeof(text));
  uint8_t encoded[512]; uint16_t length=0;
  assert(mt_mqtt_encode_radio(radio,16+sizeof(text),encoded,&length)==ESP_OK);
  const uint8_t expected[]={0x0d,0x78,0x56,0x34,0x12,0x15,0xff,0xff,0xff,0xff,
    0x35,1,0,0,0,0x18,0x7f,0x2a,9,0x08,1,0x12,5,'h','e','l','l','o'};
  assert(length==sizeof(expected) && memcmp(encoded,expected,length)==0);
  mt_mqtt_packet_t packet;
  assert(mt_mqtt_parse_packet(encoded,length,&packet)==ESP_OK && packet.is_encrypted);
  assert(packet.from==0x12345678 && packet.to==UINT32_MAX && packet.id==1 && packet.channel==0x7f);
  char output[MT_MQTT_TEXT_MAX];
  assert(mt_mqtt_parse_text(packet.payload,packet.length,output)==ESP_OK && strcmp(output,"hello")==0);
  mt_mqtt_envelope_t envelope={.packet=encoded,.packet_length=length,.channel="LongFast",.gateway="!aabbccdd"};
  uint8_t message[512]; uint16_t size=0;
  assert(mt_mqtt_encode(&envelope,message,sizeof(message),&size)==ESP_OK);
  assert(message[0]==0x0a && message[1]==length && message[length+2]==0x12);
  mt_mqtt_envelope_t decoded;
  assert(mt_mqtt_decode(message,size,&decoded)==ESP_OK);
  assert(strcmp(decoded.channel,"LongFast")==0 && strcmp(decoded.gateway,"!aabbccdd")==0);
  assert(decoded.packet_length==length && memcmp(decoded.packet,encoded,length)==0);
  for(uint16_t cut=0;cut<size;++cut) assert(mt_mqtt_decode(message,cut,&decoded)!=ESP_OK);
  assert(mt_mqtt_encode(&envelope,message,1,&size)==ESP_ERR_INVALID_SIZE);
  assert(mt_mqtt_encode_radio(radio,sizeof(radio),encoded,&length)==ESP_OK && length<=sizeof(encoded));
  const uint8_t invalid_varint[]={0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0};
  assert(mt_mqtt_parse_packet(invalid_varint,sizeof(invalid_varint),&packet)!=ESP_OK);
  const uint8_t wrong_port[]={0x08,6,0x12,1,'x'};
  assert(mt_mqtt_parse_text(wrong_port,sizeof(wrong_port),output)==ESP_ERR_NOT_SUPPORTED);
  const uint8_t embedded_nul[]={0x08,1,0x12,2,'x',0};
  assert(mt_mqtt_parse_text(embedded_nul,sizeof(embedded_nul),output)!=ESP_OK);
  const uint8_t utf8[]={0x08,1,0x12,6,0xc3,0xa1,0xf0,0x9f,0x90,0x99};
  assert(mt_mqtt_parse_text(utf8,sizeof(utf8),output)==ESP_OK);
  const uint8_t malformed[][8]={
    {0x08,1,0x12,1,0xc3}, {0x08,1,0x12,2,0xc0,0x80},
    {0x08,1,0x12,3,0xed,0xa0,0x80}, {0x08,1,0x12,4,0xf4,0x90,0x80,0x80}
  };
  for(unsigned i=0;i<4;++i)
    assert(mt_mqtt_parse_text(malformed[i],4+malformed[i][3],output)!=ESP_OK);
  uint32_t random=0x12345678;
  for(unsigned iteration=0;iteration<20000;++iteration) {
    size=iteration%sizeof(message);
    for(unsigned index=0;index<size;++index) {
      random^=random<<13; random^=random>>17; random^=random<<5; message[index]=(uint8_t)random;
    }
    if(mt_mqtt_decode(message,size,&decoded)==ESP_OK) {
      assert(decoded.packet>=message && decoded.packet+decoded.packet_length<=message+size);
    }
    if(mt_mqtt_parse_packet(message,size,&packet)==ESP_OK)
      assert(packet.payload>=message && packet.payload+packet.length<=message+size);
    (void)mt_mqtt_parse_text(message,size,output);
  }
  puts("PASS: official MeshPacket field-5/ServiceEnvelope vectors, truncation and 20,000 malformed-input cases");
}
