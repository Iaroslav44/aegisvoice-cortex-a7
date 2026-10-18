#include "av_core_adapter.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)
/* Deterministic test entropy ONLY. Production uses av_rng_fn supplied by BSP. */
static av_status test_random(void *context, uint8_t *out, size_t length)
{
    uint32_t *state=context;
    for (size_t i=0u;i<length;++i) { *state=*state*1664525u+1013904223u; out[i]=(uint8_t)(*state>>24); }
    return AV_OK;
}

static int exchange(av_firmware_app_ops *a, av_firmware_app_ops *b)
{
    uint8_t in[AV_MAX_PACKET], out[AV_MAX_PACKET];
    size_t n=sizeof(out), incoming=0u;
    bool ready=false;
    a->reset_session(a->context); b->reset_session(b->context);
    CHECK(b->begin_session(b->context,out,&n) && n==0u);
    n=sizeof(out);
    CHECK(a->begin_session(a->context,out,&n));
    for (unsigned i=0u;i<4u;++i) {
        av_firmware_app_ops *r=(i%2u)==0u ? b : a;
        memcpy(in,out,n); incoming=n; n=sizeof(out);
        CHECK(r->on_control(r->context,in,incoming,out,&n,&ready));
        CHECK(ready==(i>=2u));
    }
    CHECK(n==0u);
    return 0;
}

int main(void)
{
    uint32_t random_a=7u, random_b=19u;
    av_identity ia,ib;
    av_device da,db;
    av_core_adapter ca,cb;
    av_firmware_app_ops a,b;
    uint8_t p0[AV_MAX_PACKET], p1[AV_MAX_PACKET], pub[32];
    int16_t pcm[AV_PCM_SAMPLES], played[AV_PCM_SAMPLES];
    size_t n0=sizeof(p0),n1=sizeof(p1);
    CHECK(av_identity_generate(&ia,test_random,&random_a)==AV_OK);
    CHECK(av_identity_generate(&ib,test_random,&random_b)==AV_OK);
    CHECK(av_device_init(&da,&ia,ib.public_key,AV_ROLE_INITIATOR,test_random,&random_a)==AV_OK);
    CHECK(av_device_init(&db,&ib,ia.public_key,AV_ROLE_RESPONDER,test_random,&random_b)==AV_OK);
    CHECK(av_core_adapter_init(&ca,&da,&a)); CHECK(av_core_adapter_init(&cb,&db,&b));
    for (size_t i=0u;i<AV_PCM_SAMPLES;++i) pcm[i]=1200;
    CHECK(!a.encrypt_frame(a.context,pcm,p0,&n0) && n0==0u);
    CHECK(a.public_identity(a.context,pub) && memcmp(pub,ia.public_key,32u)==0);
    CHECK(exchange(&a,&b)==0);
    n0=sizeof(p0); n1=sizeof(p1);
    CHECK(a.encrypt_frame(a.context,pcm,p0,&n0));
    CHECK(a.encrypt_frame(a.context,pcm,p1,&n1));
    CHECK(n0==204u && n1==204u);
    CHECK(b.decrypt_frame(b.context,p1,n1,played) && played[0]==1200);
    CHECK(!b.decrypt_frame(b.context,p0,n0,played)); /* Authenticated but late. */
    for (size_t i=0u;i<AV_PCM_SAMPLES;++i) CHECK(played[i]==0);
    CHECK(!b.decrypt_frame(b.context,p1,n1,played)); /* Replay. */
    CHECK(exchange(&a,&b)==0);
    CHECK(!b.decrypt_frame(b.context,p1,n1,played)); /* Previous session. */
    n0=sizeof(p0);
    CHECK(a.encrypt_frame(a.context,pcm,p0,&n0));
    CHECK(b.decrypt_frame(b.context,p0,n0,played));
    memset(played,0x5a,sizeof(played));
    CHECK(!b.decrypt_frame(b.context,NULL,n0,played));
    for (size_t i=0u;i<AV_PCM_SAMPLES;++i) CHECK(played[i]==0);
    /* In-place capture failure must erase encoded speech and publish no bytes. */
    da.secure.tx_sequence=UINT64_MAX;
    memset(p0,0xa5,sizeof(p0)); n0=sizeof(p0);
    CHECK(av_device_capture(&da,pcm,p0,&n0)==AV_ERR_EXHAUSTED && n0==0u);
    for (size_t i=0u;i<AV_ADPCM_BYTES;++i) CHECK(p0[AV_SECURE_HEADER+i]==0u);
    /* Decode errors (including authenticated invalid ADPCM) supply full silence. */
    memset(played,0x5a,sizeof(played)); p1[0]^=1u;
    CHECK(!b.decrypt_frame(b.context,p1,n1,played));
    for (size_t i=0u;i<AV_PCM_SAMPLES;++i) CHECK(played[i]==0);
    CHECK(exchange(&a,&b)==0);
    {
        uint8_t malformed[AV_ADPCM_BYTES]={0}; uint32_t timestamp=99u;
        malformed[2]=89u;
        CHECK(av_secure_seal(&da.secure,0u,malformed,sizeof(malformed),p0,sizeof(p0),&n0)==AV_OK);
        memset(played,0x5a,sizeof(played));
        CHECK(av_device_playback(&db,p0,n0,played,&timestamp)==AV_ERR_FORMAT && timestamp==0u);
        for (size_t i=0u;i<AV_PCM_SAMPLES;++i) CHECK(played[i]==0);
    }
    av_device_destroy(&da); av_device_destroy(&db);
    av_identity_destroy(&ia); av_identity_destroy(&ib);
    puts("device adapter: authentication gate, duplex state, late/replay/old-session rejection passed");
    return 0;
}
