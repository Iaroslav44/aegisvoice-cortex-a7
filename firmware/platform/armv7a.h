#ifndef AV_ARMV7A_H
#define AV_ARMV7A_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if !defined(__arm__) || !defined(__GNUC__)
#error "ARMv7-A helpers require a GCC-compatible ARM target compiler"
#endif

static inline void av_dmb(void) { __asm volatile("dmb sy" ::: "memory"); }
static inline void av_dsb(void) { __asm volatile("dsb sy" ::: "memory"); }
static inline void av_isb(void) { __asm volatile("isb" ::: "memory"); }

typedef enum {
    AV_CACHE_CLEAN,
    AV_CACHE_INVALIDATE,
    AV_CACHE_CLEAN_INVALIDATE
} av_cache_operation;

/* Privileged ARMv7-A maintenance by MVA to Point of Coherency. Regions MUST
 * consist of whole, exclusively owned cache lines. Misalignment is rejected:
 * silently rounding a DMA region may destroy adjacent CPU-owned dirty data.
 * Virtual addresses and MMU mapping must remain valid/stable for the call.
 * BSP adds outer-cache maintenance if its memory system requires it. */
bool av_cache_region(void *base, size_t length, av_cache_operation operation);
size_t av_cache_line_bytes(void);

#endif
