#include "dma_buffer.h"
#include "armv7a.h"

static bool maintain(av_dma_lease *lease, av_cache_operation operation)
{
    return lease != NULL && lease->block != NULL &&
        av_cache_line_bytes() <= AV_DMA_ALIGNMENT &&
        av_cache_region(lease->block, sizeof(*lease->block), operation);
}

bool av_dma_begin_rx(av_dma_lease *lease)
{
    if (lease == NULL || lease->owner != AV_DMA_CPU ||
        !maintain(lease, AV_CACHE_CLEAN_INVALIDATE)) return false;
    lease->owner = AV_DMA_DEVICE_RX;
    av_dmb();
    return true;
}

bool av_dma_complete_rx(av_dma_lease *lease)
{
    if (lease == NULL || lease->owner != AV_DMA_DEVICE_RX ||
        !maintain(lease, AV_CACHE_INVALIDATE)) return false;
    lease->owner = AV_DMA_CPU;
    av_dmb();
    return true;
}

bool av_dma_begin_tx(av_dma_lease *lease)
{
    if (lease == NULL || lease->owner != AV_DMA_CPU ||
        !maintain(lease, AV_CACHE_CLEAN)) return false;
    lease->owner = AV_DMA_DEVICE_TX;
    av_dmb();
    return true;
}

bool av_dma_complete_tx(av_dma_lease *lease)
{
    if (lease == NULL || lease->block == NULL || lease->owner != AV_DMA_DEVICE_TX)
        return false;
    av_dsb();
    lease->owner = AV_DMA_CPU;
    return true;
}
