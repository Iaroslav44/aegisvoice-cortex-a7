#ifndef AV_DEVICE_H
#define AV_DEVICE_H
#include "av/secure.h"

/* Owned by the media task. No heap, shared mutable session, or plaintext log. */
typedef struct {
    av_secure_session secure;
    const av_identity *identity;
    uint8_t peer[32];
    av_rng_fn random;
    void *random_context;
    av_role role;
    uint32_t next_sample;
} av_device;

av_status av_device_init(av_device *device, const av_identity *identity,
                         const uint8_t peer[32], av_role role,
                         av_rng_fn random, void *random_context);
av_status av_device_reset(av_device *device);
/* Input/output buffers and device storage must be disjoint. Captured ADPCM is
 * encrypted in place inside packet; no plaintext is retained there on failure.
 * Caller owns packet until all queued transport users have finished with it. */
av_status av_device_capture(av_device *device, const int16_t pcm[AV_PCM_SAMPLES],
                            uint8_t packet[AV_MAX_PACKET], size_t *length);
/* With valid arguments, all 320 output samples are written on success; any
 * packet rejection returns silence. Plaintext scratch is erased before return. */
av_status av_device_playback(av_device *device, const uint8_t *packet, size_t length,
                             int16_t pcm[AV_PCM_SAMPLES], uint32_t *sample_timestamp);
void av_device_destroy(av_device *device);
#endif
