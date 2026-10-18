#include "av/device.h"
#include "av/audio.h"
#include "monocypher.h"
#include <string.h>

av_status av_device_init(av_device *device, const av_identity *identity,
                         const uint8_t peer[32], av_role role,
                         av_rng_fn random, void *random_context)
{
    av_status result;
    if (!device || !identity || !peer || !random) return AV_ERR_ARGUMENT;
    memset(device,0,sizeof(*device));
    result=av_secure_init(&device->secure,identity,peer,role,random,random_context);
    if (result!=AV_OK) return result;
    device->identity=identity;
    memcpy(device->peer,peer,32);
    device->role=role;
    device->random=random;
    device->random_context=random_context;
    return AV_OK;
}

av_status av_device_reset(av_device *device)
{
    if (!device || !device->identity || !device->random) return AV_ERR_ARGUMENT;
    /* av_secure_init erases the old session before installing the new state. */
    device->next_sample=0u;
    return av_secure_init(&device->secure,device->identity,device->peer,
        device->role,device->random,device->random_context);
}

av_status av_device_capture(av_device *device, const int16_t pcm[AV_PCM_SAMPLES],
                            uint8_t packet[AV_MAX_PACKET], size_t *length)
{
    uint8_t *adpcm;
    size_t encoded=0u;
    av_status result;
    if (length) *length=0u;
    if (!device || !pcm || !packet || !length) return AV_ERR_ARGUMENT;
    if (!av_secure_ready(&device->secure)) return AV_ERR_STATE;
    /* Encode into the future ciphertext region. The pinned AEAD implementation
     * supports exact in-place encryption; no transient plaintext copy needed. */
    adpcm=packet+AV_SECURE_HEADER;
    result=av_audio_encode(pcm,AV_PCM_SAMPLES,adpcm,AV_ADPCM_BYTES,&encoded);
    if (result==AV_OK) {
        result=av_secure_seal(&device->secure,device->next_sample,adpcm,encoded,
            packet,AV_MAX_PACKET,length);
        /* Advance sample time even when a captured frame cannot be transmitted. */
        device->next_sample+=AV_PCM_SAMPLES;
    }
    /* A refused seal (e.g. exhausted counter) must not leave encoded speech in
     * the caller's packet buffer. Successful seal has replaced it completely. */
    if (result!=AV_OK) crypto_wipe(adpcm,AV_ADPCM_BYTES);
    return result;
}

av_status av_device_playback(av_device *device, const uint8_t *packet, size_t length,
                             int16_t pcm[AV_PCM_SAMPLES], uint32_t *sample_timestamp)
{
    uint8_t adpcm[AV_MAX_PAYLOAD];
    size_t decoded=0u, samples=0u;
    av_status result;
    if (!device || !packet || !pcm || !sample_timestamp) return AV_ERR_ARGUMENT;
    *sample_timestamp=0u;
    result=av_secure_open(&device->secure,packet,length,adpcm,sizeof(adpcm),
        &decoded,sample_timestamp);
    if (result==AV_OK) result=av_audio_decode(adpcm,decoded,pcm,AV_PCM_SAMPLES,&samples);
    if (result!=AV_OK) {
        memset(pcm,0,AV_PCM_SAMPLES*sizeof(*pcm));
        *sample_timestamp=0u;
    }
    crypto_wipe(adpcm,sizeof(adpcm));
    return result;
}

void av_device_destroy(av_device *device)
{
    /* Includes the entire session; a second nested wipe adds no protection. */
    if (device) crypto_wipe(device,sizeof(*device));
}
