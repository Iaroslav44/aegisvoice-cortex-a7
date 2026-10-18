#ifndef AV_DMA_BUFFER_H
#define AV_DMA_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>
#include "av/config.h"

#define AV_DMA_ALIGNMENT 64u

/* The payload's size and alignment prevent sharing a line with metadata.
 * Linker placement must preserve alignment and use DMA-accessible RAM. */
typedef struct {
    alignas(AV_DMA_ALIGNMENT) int16_t samples[AV_PCM_SAMPLES];
} av_dma_audio_block;
_Static_assert(sizeof(av_dma_audio_block) % AV_DMA_ALIGNMENT == 0,
               "DMA payload must occupy complete cache lines");

typedef enum { AV_DMA_CPU, AV_DMA_DEVICE_RX, AV_DMA_DEVICE_TX } av_dma_owner;
typedef struct {
    av_dma_audio_block *block;
    av_dma_owner owner;
} av_dma_lease;

/* Single core. Caller must serialize each lease against its ISR (critical
 * section/API-safe IRQ masking). Ownership is a protocol, not an atomic lock.
 * Successful begin is followed by programming DMA physical address/descriptor.
 * Complete may be called ONLY after hardware completion + device ordering.
 * Do not put lease metadata in the cache lines occupied by the payload. */
bool av_dma_begin_rx(av_dma_lease *lease);
bool av_dma_complete_rx(av_dma_lease *lease);
bool av_dma_begin_tx(av_dma_lease *lease);
bool av_dma_complete_tx(av_dma_lease *lease);

#endif
