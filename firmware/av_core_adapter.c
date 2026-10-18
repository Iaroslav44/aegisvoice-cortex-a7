#include "av_core_adapter.h"
#include <string.h>

static void reset_session(void *context)
{
    av_core_adapter *adapter=context;
    adapter->have_sample=false;
    adapter->last_sample=0u;
    adapter->reset_ok=av_device_reset(adapter->device)==AV_OK;
}

static bool begin_session(void *context, uint8_t *packet, size_t *length)
{
    av_core_adapter *adapter=context;
    size_t capacity=*length;
    *length=0u;
    if (!adapter->reset_ok) return false;
    if (adapter->device->role==AV_ROLE_RESPONDER) return true;
    return av_secure_handshake(&adapter->device->secure,NULL,0u,
        packet,capacity,length)==AV_OK;
}

static bool on_control(void *context, const uint8_t *packet, size_t length,
                        uint8_t *response, size_t *response_length, bool *authenticated)
{
    av_core_adapter *adapter=context;
    size_t capacity=*response_length;
    *authenticated=false;
    *response_length=0u;
    if (!adapter->reset_ok) return false;
    if (av_secure_handshake(&adapter->device->secure,packet,length,response,
        capacity,response_length)!=AV_OK) return false;
    *authenticated=av_secure_ready(&adapter->device->secure);
    return true;
}

static bool encrypt_frame(void *context, const int16_t pcm[AV_PCM_SAMPLES],
                           uint8_t *packet, size_t *length)
{
    av_core_adapter *adapter=context;
    size_t capacity=*length;
    *length=0u;
    if (capacity<AV_MAX_PACKET || !adapter->reset_ok) return false;
    return av_device_capture(adapter->device,pcm,packet,length)==AV_OK;
}

static bool decrypt_frame(void *context, const uint8_t *packet, size_t length,
                           int16_t pcm[AV_PCM_SAMPLES])
{
    av_core_adapter *adapter=context;
    uint32_t timestamp=0u;
    if (!adapter->reset_ok || packet==NULL) {
        memset(pcm,0,AV_PCM_SAMPLES*sizeof(*pcm));
        return false;
    }
    /* Playback writes every sample on success and supplies silence on failure.
     * Pre-clearing here would write the same 640-byte buffer a second time. */
    if (av_device_playback(adapter->device,packet,length,pcm,&timestamp)!=AV_OK) return false;
    if (adapter->have_sample) {
        uint32_t delta=timestamp-adapter->last_sample;
        /* Bounded FIFO cannot reorder. Reject late audio and discontinuities.
         * Modular sample time supports wrap; gaps may span multiple frames.
         * A gap is not concealed here: playout uses silence only on underrun. */
        if (delta==0u || delta>=UINT32_C(0x80000000) || delta%AV_PCM_SAMPLES!=0u) {
            memset(pcm,0,AV_PCM_SAMPLES*sizeof(*pcm));
            return false;
        }
    }
    adapter->have_sample=true;
    adapter->last_sample=timestamp;
    return true;
}

static bool public_identity(void *context, uint8_t identity[32])
{
    av_core_adapter *adapter=context;
    if (!adapter->device->identity) return false;
    memcpy(identity,adapter->device->identity->public_key,32u);
    return true;
}

bool av_core_adapter_init(av_core_adapter *adapter, av_device *device,
                          av_firmware_app_ops *ops)
{
    if (!adapter || !device || !device->identity || !ops) return false;
    memset(adapter,0,sizeof(*adapter));
    adapter->device=device;
    adapter->reset_ok=true;
    ops->context=adapter;
    ops->reset_session=reset_session;
    ops->begin_session=begin_session;
    ops->on_control=on_control;
    ops->encrypt_frame=encrypt_frame;
    ops->decrypt_frame=decrypt_frame;
    ops->public_identity=public_identity;
    return true;
}
