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
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <inttypes.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_SIZE 1
#define ESP_ERR_TIMEOUT 2
#define ESP_ERR_INVALID_STATE 3
#define ESP_ERR_INVALID_RESPONSE 4
#define SCF_CMD_ADTC 1
#define SCF_RSP_R1 2
#define SDMMC_WRITE_CMD_TIMEOUT_MS 5000
#define MMC_WRITE_BLOCK_SINGLE 24
#define MMC_WRITE_BLOCK_MULTIPLE 25
#define SD_OCR_SDHC_CAP 1
#define SD_SPI_R2_CARD_LOCKED 1
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef struct { struct { size_t capacity, sector_size; } csd; uint32_t ocr; bool spi; } sdmmc_card_t;
typedef struct { int flags; size_t blklen,datalen,buflen; void *data; int timeout_ms,opcode; size_t arg; } sdmmc_command_t;
static int write_error,status_error,recovery_error,idle_error;
static unsigned writes,recoveries,idles,statuses;
static size_t written_blocks;
static sdmmc_command_t last;
static bool host_is_spi(sdmmc_card_t *c) { return c->spi; }
static void vTaskDelay(int ticks) { assert(ticks == 1); }
static int sdmmc_send_cmd(sdmmc_card_t *c,sdmmc_command_t *cmd) { (void)c;last=*cmd;++writes;return write_error; }
static int sdmmc_send_cmd_send_status(sdmmc_card_t *c,uint32_t *status) { (void)c;*status=0;++statuses;return status_error; }
static int sdmmc_wait_for_idle(sdmmc_card_t *c,uint32_t status) { (void)c;(void)status;++idles;return idle_error; }
static int sdmmc_send_cmd_num_of_written_blocks(sdmmc_card_t *c,size_t *n) { (void)c;++recoveries;*n=written_blocks;return recovery_error; }
#include "write_function.inc"
int main(void) {
 sdmmc_card_t card={.csd={1024,512},.ocr=SD_OCR_SDHC_CAP};uint8_t data[32768]={0};
 for(int spi=0;spi<2;++spi) {
  card.spi=spi;
  for(int count=1;count<=64;count+=63) {
   writes=recoveries=idles=statuses=0;
   write_error=status_error=recovery_error=idle_error=0;
   assert(sdmmc_write_sectors_dma(&card,data,7,count,sizeof(data))==ESP_OK);
   assert(writes==1 && recoveries==0 && last.arg==7 && last.datalen==(size_t)count*512);
   assert(last.opcode==(count==1?24:25));
   for(size_t partial=0;partial<=(size_t)count;++partial) {
    for(int recovery=0;recovery<=4;++recovery) {
     written_blocks=partial;recovery_error=recovery;write_error=ESP_ERR_TIMEOUT;
     assert(sdmmc_write_sectors_dma(&card,data,7,count,sizeof(data))==ESP_ERR_TIMEOUT);
    }
   }
   write_error=0;status_error=ESP_ERR_INVALID_RESPONSE;
   unsigned old_idle=idles;
   assert(sdmmc_write_sectors_dma(&card,data,7,count,sizeof(data))==ESP_ERR_INVALID_RESPONSE);
   assert(idles==old_idle);
   status_error=0;idle_error=ESP_ERR_TIMEOUT;
   assert(sdmmc_write_sectors_dma(&card,data,7,count,sizeof(data))==(spi?ESP_OK:ESP_ERR_TIMEOUT));
  }
 }
 unsigned old_writes=writes;
 assert(sdmmc_write_sectors_dma(&card,data,1020,64,sizeof(data))==ESP_ERR_INVALID_SIZE);
 assert(writes==old_writes);
 puts("PASS: actual SDK write function: single/multiblock, SD/SPI, partial writes, recovery/status failures and busy timeout");
}
