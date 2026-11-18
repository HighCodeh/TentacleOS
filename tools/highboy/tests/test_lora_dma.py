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



from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[3]
source=(ROOT/'firmware_p4/components/Drivers/sx1262/sx1262_hal.c').read_text()
a=source.index('static int hal_spi_transfer(');b=source.index('static void hal_cs_low(',a)
function=source[a:b]
fixture=r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
typedef int esp_err_t;
typedef struct {void *spi;} hal_esp32_ctx_t;
typedef struct {size_t length;const void *tx_buffer;void *rx_buffer;unsigned flags;} spi_transaction_t;
#define ESP_OK 0
#define SPI_MAX_TRANSFER 264
#define SPI_TRANS_DMA_USE_PSRAM 4096
static uint8_t s_dma_tx[SPI_MAX_TRANSFER],s_dma_rx[SPI_MAX_TRANSFER];
static uint8_t input[264],output[264];
static size_t expected_length;
static bool_t_dummy;
static unsigned calls;
static int result;
static int spi_device_polling_transmit(void *spi,spi_transaction_t *t){
 assert(spi==(void *)1 && t->length==expected_length*8);calls++;
#if CONFIG_IDF_TARGET_ESP32P4
 assert(t->flags==SPI_TRANS_DMA_USE_PSRAM);
 if(t->tx_buffer)assert(t->tx_buffer==s_dma_tx);
 if(t->rx_buffer)assert(t->rx_buffer==s_dma_rx);
#else
 assert(t->flags==0);
 if(t->tx_buffer)assert(t->tx_buffer==input);
 if(t->rx_buffer)assert(t->rx_buffer==output);
#endif
 if(t->tx_buffer)assert(memcmp(t->tx_buffer,input,expected_length)==0);
 if(t->rx_buffer)memset(t->rx_buffer,0x5a,expected_length);
 return result;
}
/* FUNCTION */
int main(void){
 hal_esp32_ctx_t ctx={.spi=(void *)1};
 assert(hal_spi_transfer(NULL,input,output,4)==-1);
 assert(hal_spi_transfer(&ctx,NULL,NULL,4)==-1);
 assert(hal_spi_transfer(&ctx,input,output,0)==-1);
 assert(hal_spi_transfer(&ctx,input,output,265)==-1 && calls==0);
 for(size_t n=1;n<=264;n++){
  expected_length=n;memset(input,0xa5,sizeof(input));memset(output,0xcc,sizeof(output));
  assert(hal_spi_transfer(&ctx,input,output,n)==0);
  for(size_t i=0;i<n;i++)assert(output[i]==0x5a);
  for(size_t i=n;i<264;i++)assert(output[i]==0xcc);
  assert(hal_spi_transfer(&ctx,input,NULL,n)==0);
  assert(hal_spi_transfer(&ctx,NULL,output,n)==0);
 }
#if CONFIG_IDF_TARGET_ESP32P4
 result=-1;memset(output,0xcc,sizeof(output));expected_length=4;
 assert(hal_spi_transfer(&ctx,input,output,4)==-1 && output[0]==0xcc);
#endif
 puts("PASS SX1262 exact 1..264-byte transfers, bounds, DMA routing and RX copy semantics");
}
""".replace('static bool_t_dummy;','')
with tempfile.TemporaryDirectory() as tmp:
 d=Path(tmp);(d/'test.c').write_text(fixture.replace('/* FUNCTION */',function))
 for target in (0,1):
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-variable',
   '-fsanitize=undefined','-fno-sanitize-recover=all',f'-DCONFIG_IDF_TARGET_ESP32P4={target}',
   '-DCONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=1',str(d/'test.c'),'-o',str(d/'test')],check=True)
  subprocess.run([str(d/'test')],check=True,timeout=10)
