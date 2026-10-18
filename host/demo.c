#define _CRT_SECURE_NO_WARNINGS
#include "av/device.h"
#include "av/transport.h"
#include "random.h"
#include "wav.h"
#include "monocypher.h"
#include <stdio.h>
#include <string.h>

/* Synthetic phone: owns ciphertext only. A real relay forwards these same bytes. */
typedef struct { uint16_t id; uint32_t fragments; uint32_t bytes; size_t mtu; } relay;

static av_status relay_packet(relay *phone, const uint8_t *packet, size_t length,
                              uint8_t delivered[AV_MAX_PACKET], size_t *delivered_length)
{
    av_fragmenter tx;
    av_reassembly rx;
    uint8_t fragment[AV_ATT_PAYLOAD_MAX];
    size_t n=0u;
    av_status rc;
    *delivered_length=0u;
    av_reassembly_init(&rx);
    rc=av_fragmenter_init(&tx,phone->id++,packet,length);
    if (rc!=AV_OK) return rc;
    while ((rc=av_fragmenter_next(&tx,phone->mtu,fragment,sizeof(fragment),&n))==AV_OK) {
        ++phone->fragments;
        phone->bytes+=(uint32_t)n;
        rc=av_reassembly_accept(&rx,fragment,n,0u,delivered,AV_MAX_PACKET,delivered_length);
        if (rc!=AV_OK) return rc;
    }
    return rc==AV_ERR_EMPTY && *delivered_length==length ? AV_OK : AV_ERR_STATE;
}

static av_status handshake(av_device *a, av_device *b, relay *phone)
{
    uint8_t out[AV_MAX_PACKET], in[AV_MAX_PACKET];
    size_t out_n=0u, in_n=0u;
    av_status rc=av_secure_handshake(&a->secure,NULL,0u,out,sizeof(out),&out_n);
    for (unsigned step=0u;rc==AV_OK && step<4u;++step) {
        av_device *receiver=(step%2u)==0u ? b : a;
        rc=relay_packet(phone,out,out_n,in,&in_n);
        if (rc==AV_OK) rc=av_secure_handshake(&receiver->secure,in,in_n,out,sizeof(out),&out_n);
    }
    if (rc!=AV_OK) return rc;
    return av_secure_ready(&a->secure) && av_secure_ready(&b->secure) ? AV_OK : AV_ERR_STATE;
}

static int run_demo(const char *input_path, const char *output_path, size_t mtu)
{
    av_identity alice={0}, bob={0};
    av_device a={0}, b={0};
    relay phone={0};
    av_wav input={0}, output={0};
    int16_t pcm[AV_PCM_SAMPLES], played[AV_PCM_SAMPLES];
    uint8_t packet[AV_MAX_PACKET], forwarded[AV_MAX_PACKET];
    size_t packet_n=0u, forwarded_n=0u, count=AV_PCM_SAMPLES;
    uint32_t timestamp=0u, frames=0u;
    int success=0;
    av_status rc;
    phone.mtu=mtu;
#define REQUIRE(call) do { rc=(call); if(rc!=AV_OK) { fprintf(stderr,"%s: error %d\n",#call,(int)rc); goto cleanup; } } while(0)
    REQUIRE(av_identity_generate(&alice,av_host_random,NULL));
    REQUIRE(av_identity_generate(&bob,av_host_random,NULL));
    /* In hardware these are locally provisioned, authenticated public-key pins. */
    REQUIRE(av_device_init(&a,&alice,bob.public_key,AV_ROLE_INITIATOR,av_host_random,NULL));
    REQUIRE(av_device_init(&b,&bob,alice.public_key,AV_ROLE_RESPONDER,av_host_random,NULL));
    REQUIRE(handshake(&a,&b,&phone));
    puts("Mutually authenticated X25519 / Ed25519 session established; phone has no keys.");
    if (input_path) REQUIRE(av_wav_read_open(&input,input_path));
    if (output_path) REQUIRE(av_wav_write_open(&output,output_path));
    for (;;) {
        if (input_path) {
            REQUIRE(av_wav_read(&input,pcm,AV_PCM_SAMPLES,&count));
            if (count==0u) break;
            memset(pcm+count,0,(AV_PCM_SAMPLES-count)*sizeof(*pcm));
        } else {
            if (frames==150u) break;
            for (size_t i=0u;i<AV_PCM_SAMPLES;++i) {
                /* Integer triangle, no platform DSP/libm dependency. */
                uint32_t phase=(frames*AV_PCM_SAMPLES+(uint32_t)i)%80u;
                pcm[i]=(int16_t)(phase<40u ? (int32_t)phase*500-10000 : 30000-(int32_t)phase*500);
            }
        }
        REQUIRE(av_device_capture(&a,pcm,packet,&packet_n));
        REQUIRE(relay_packet(&phone,packet,packet_n,forwarded,&forwarded_n));
        if (frames==0u) {
            forwarded[AV_SECURE_HEADER]^=1u;
            rc=av_device_playback(&b,forwarded,forwarded_n,played,&timestamp);
            forwarded[AV_SECURE_HEADER]^=1u;
            if (rc!=AV_ERR_AUTH) { fputs("Tamper rejection failed\n",stderr); goto cleanup; }
        }
        REQUIRE(av_device_playback(&b,forwarded,forwarded_n,played,&timestamp));
        if (timestamp!=frames*AV_PCM_SAMPLES) { fputs("Sample clock mismatch\n",stderr); goto cleanup; }
        if (output_path) REQUIRE(av_wav_write(&output,played,count));
        if (frames==0u) {
            rc=av_device_playback(&b,forwarded,forwarded_n,played,&timestamp);
            if (rc!=AV_ERR_REPLAY) { fputs("Replay rejection failed\n",stderr); goto cleanup; }
            puts("Modified ciphertext and replayed packet rejected.");
        }
        for (size_t i=0u;i<AV_PCM_SAMPLES;++i) pcm[i]=(int16_t)(-(int32_t)pcm[i]/2);
        REQUIRE(av_device_capture(&b,pcm,packet,&packet_n));
        REQUIRE(relay_packet(&phone,packet,packet_n,forwarded,&forwarded_n));
        REQUIRE(av_device_playback(&a,forwarded,forwarded_n,played,&timestamp));
        if (timestamp!=frames*AV_PCM_SAMPLES) { fputs("Reverse sample clock mismatch\n",stderr); goto cleanup; }
        ++frames;
    }
    REQUIRE(av_wav_close(&output,1));
    REQUIRE(av_wav_close(&input,0));
    /* Old ciphertext must not survive a fresh handshake after reconnect. */
    REQUIRE(av_device_reset(&a)); REQUIRE(av_device_reset(&b)); REQUIRE(handshake(&a,&b,&phone));
    if (frames!=0u && av_device_playback(&a,forwarded,forwarded_n,played,&timestamp)==AV_OK) {
        fputs("Old-session packet accepted\n",stderr); goto cleanup;
    }
    printf("PASS: %lu frames per direction, %lu GATT fragments, %lu transmitted bytes (including handshakes).\n",
        (unsigned long)frames,(unsigned long)phone.fragments,(unsigned long)phone.bytes);
    printf("20 ms frames; 164-byte ADPCM + 40-byte AEAD = 204-byte packets; ATT value capacity %lu.\n",(unsigned long)mtu);
    puts("Host simulation only: no claims about Cortex-A7 timing or physical Bluetooth throughput.");
    success=1;
cleanup:
    (void)av_wav_close(&output,1); (void)av_wav_close(&input,0);
    av_device_destroy(&a); av_device_destroy(&b);
    av_identity_destroy(&alice); av_identity_destroy(&bob);
    crypto_wipe(pcm,sizeof(pcm)); crypto_wipe(played,sizeof(played));
    return success ? 0 : 1;
#undef REQUIRE
}

int main(int argc, char **argv)
{
    if (argc==2 && strcmp(argv[1],"--self-test")==0) {
        if (run_demo(NULL,NULL,244u)!=0) return 1;
        return run_demo(NULL,NULL,20u);
    }
    if (argc==1) return run_demo(NULL,"demo_output.wav",244u);
    if (argc==3 && strcmp(argv[1],"--output")==0) return run_demo(NULL,argv[2],244u);
    if (argc==3 && strcmp(argv[1],argv[2])!=0) return run_demo(argv[1],argv[2],244u);
    fputs("Usage: aegisvoice_demo [--self-test | --output output.wav | input.wav output.wav]\nInput: PCM16 mono 16000 Hz WAV; use distinct input/output paths.\n",stderr);
    return 2;
}
