#ifndef AV_HOST_WAV_H
#define AV_HOST_WAV_H
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include "av/status.h"
typedef struct { FILE *file; uint32_t remaining; uint32_t written; } av_wav;
av_status av_wav_read_open(av_wav *wav, const char *path);
av_status av_wav_read(av_wav *wav, int16_t *samples, size_t capacity, size_t *count);
av_status av_wav_write_open(av_wav *wav, const char *path);
av_status av_wav_write(av_wav *wav, const int16_t *samples, size_t count);
av_status av_wav_close(av_wav *wav, int writing);
#endif
