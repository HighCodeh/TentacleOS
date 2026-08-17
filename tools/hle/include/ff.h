#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int FRESULT;
typedef int FATFS;
typedef int FIL;
typedef int DIR;

#define FR_OK 0
#define FA_READ 0x01
#define FA_WRITE 0x02
#define FA_CREATE_NEW 0x04

FRESULT f_mount(FATFS *fs, const char *path, int opt);
FRESULT f_open(FIL *fp, const char *path, BYTE mode);
FRESULT f_close(FIL *fp);
FRESULT f_read(FIL *fp, void *buff, unsigned int btr, unsigned int *br);
FRESULT f_write(FIL *fp, const void *buff, unsigned int btw, unsigned int *bw);

#ifdef __cplusplus
}
#endif
