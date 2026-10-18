#ifndef AV_AUDIO_H
#define AV_AUDIO_H

#include <stddef.h>
#include <stdint.h>
#include "av/config.h"
#include "av/status.h"

/* Independent 20 ms mono frames. The first sample is a signed LE predictor,
 * followed by step index and zero reserved byte. Nibbles are low-first;
 * the final high nibble is zero padding. No state crosses frame boundaries.
 * Input/output buffers must not overlap. Output length is zero on failure. */
av_status av_audio_encode(const int16_t pcm[AV_PCM_SAMPLES], size_t sample_count,
                          uint8_t encoded[AV_ADPCM_BYTES], size_t encoded_capacity,
                          size_t *encoded_len);
av_status av_audio_decode(const uint8_t encoded[AV_ADPCM_BYTES], size_t encoded_len,
                          int16_t pcm[AV_PCM_SAMPLES], size_t pcm_capacity_samples,
                          size_t *sample_count);

#endif
