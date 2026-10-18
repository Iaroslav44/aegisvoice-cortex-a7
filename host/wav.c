#define _CRT_SECURE_NO_WARNINGS
#include "wav.h"
#include "av/config.h"
#include <string.h>

static uint16_t u16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static void p16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void p32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }

av_status av_wav_read_open(av_wav *wav, const char *path)
{
    uint8_t h[16];
    int fmt_ok = 0;
    memset(wav, 0, sizeof(*wav));
    wav->file = fopen(path, "rb");
    if (!wav->file) return AV_ERR_IO;
    if (fread(h,1,12,wav->file)!=12 || memcmp(h,"RIFF",4)!=0 || memcmp(h+8,"WAVE",4)!=0) goto bad;
    while (fread(h,1,8,wav->file)==8) {
        uint32_t n=u32(h+4);
        if (memcmp(h,"fmt ",4)==0) {
            if (n<16u || fread(h,1,16,wav->file)!=16) goto bad;
            fmt_ok = u16(h)==1u && u16(h+2)==1u && u32(h+4)==AV_SAMPLE_RATE &&
                u32(h+8)==AV_SAMPLE_RATE*2u && u16(h+12)==2u && u16(h+14)==16u;
            n -= 16u;
        } else if (memcmp(h,"data",4)==0) {
            if (!fmt_ok || (n & 1u)!=0u) goto bad;
            wav->remaining=n;
            return AV_OK;
        }
        /* Bound seek to portable signed long; never accept an overflowing chunk. */
        if (n > 0x7ffffffeu || fseek(wav->file,(long)(n+(n&1u)),SEEK_CUR)!=0) goto bad;
    }
bad:
    (void)fclose(wav->file); wav->file=NULL;
    return AV_ERR_FORMAT;
}

av_status av_wav_read(av_wav *wav, int16_t *samples, size_t capacity, size_t *count)
{
    *count=0u;
    while (*count<capacity && wav->remaining>=2u) {
        uint8_t b[2];
        uint16_t v;
        if (fread(b,1,2,wav->file)!=2) return AV_ERR_IO;
        v=u16(b);
        samples[*count]=(int16_t)(v<32768u ? (int32_t)v : (int32_t)v-65536);
        ++*count;
        wav->remaining-=2u;
    }
    return AV_OK;
}

av_status av_wav_write_open(av_wav *wav, const char *path)
{
    uint8_t h[44]={0};
    memset(wav,0,sizeof(*wav));
    /* Exclusive creation also prevents truncating input through a path alias. */
    wav->file=fopen(path,"wbx");
    if (!wav->file) return AV_ERR_IO;
    memcpy(h,"RIFF",4); memcpy(h+8,"WAVEfmt ",8); p32(h+16,16);
    p16(h+20,1); p16(h+22,1); p32(h+24,AV_SAMPLE_RATE);
    p32(h+28,AV_SAMPLE_RATE*2u); p16(h+32,2); p16(h+34,16); memcpy(h+36,"data",4);
    if (fwrite(h,1,sizeof(h),wav->file)!=sizeof(h)) {
        (void)fclose(wav->file); wav->file=NULL; return AV_ERR_IO;
    }
    return AV_OK;
}

av_status av_wav_write(av_wav *wav, const int16_t *samples, size_t count)
{
    if (count > (UINT32_MAX-36u-wav->written)/2u) return AV_ERR_SIZE;
    for (size_t i=0;i<count;++i) {
        uint8_t b[2]; p16(b,(uint16_t)samples[i]);
        if (fwrite(b,1,2,wav->file)!=2) return AV_ERR_IO;
        wav->written+=2u;
    }
    return AV_OK;
}

av_status av_wav_close(av_wav *wav, int writing)
{
    av_status result=AV_OK;
    uint8_t b[4];
    if (!wav->file) return AV_OK;
    if (writing) {
        p32(b,wav->written+36u);
        if (fseek(wav->file,4,SEEK_SET)!=0 || fwrite(b,1,4,wav->file)!=4) result=AV_ERR_IO;
        p32(b,wav->written);
        if (fseek(wav->file,40,SEEK_SET)!=0 || fwrite(b,1,4,wav->file)!=4) result=AV_ERR_IO;
    }
    if (fclose(wav->file)!=0) result=AV_ERR_IO;
    wav->file=NULL;
    return result;
}
