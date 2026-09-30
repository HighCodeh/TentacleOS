#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *HMP3Decoder;

enum {
    ERR_MP3_NONE = 0,
    ERR_MP3_INDATA_UNDERFLOW = -1,
    ERR_MP3_MAINDATA_UNDERFLOW = -2,
    ERR_MP3_NULL_POINTER = -5,
};

typedef struct _MP3FrameInfo {
    int bitrate;
    int nChans;
    int samprate;
    int bitsPerSample;
    int outputSamps;
    int layer;
    int version;
} MP3FrameInfo;

// The Helix decoder is an IDF managed component, not built for the host.
// MP3InitDecoder fails, so the player reports that it cannot decode.
static inline HMP3Decoder MP3InitDecoder(void) { return NULL; }
static inline void MP3FreeDecoder(HMP3Decoder dec) { (void)dec; }
static inline int MP3Decode(HMP3Decoder dec, unsigned char **inbuf, int *bytes_left,
                            short *outbuf, int use_size) {
    (void)dec;
    (void)inbuf;
    (void)bytes_left;
    (void)outbuf;
    (void)use_size;
    return ERR_MP3_NULL_POINTER;
}
static inline void MP3GetLastFrameInfo(HMP3Decoder dec, MP3FrameInfo *info) {
    (void)dec;
    if (info) {
        info->bitrate = 0;
        info->nChans = 0;
        info->samprate = 0;
        info->bitsPerSample = 0;
        info->outputSamps = 0;
        info->layer = 0;
        info->version = 0;
    }
}
static inline int MP3FindSyncWord(unsigned char *buf, int n_bytes) {
    (void)buf;
    (void)n_bytes;
    return -1;
}

#ifdef __cplusplus
}
#endif
