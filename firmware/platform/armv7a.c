#include "armv7a.h"

size_t av_cache_line_bytes(void)
{
    uint32_t ctr;
    __asm volatile("mrc p15, 0, %0, c0, c0, 1" : "=r"(ctr));
    return (size_t)4u << ((ctr >> 16) & 15u); /* CTR.DminLine: log2(words). */
}

bool av_cache_region(void *base, size_t length, av_cache_operation operation)
{
    const size_t line = av_cache_line_bytes();
    uintptr_t address = (uintptr_t)base;
    if (base == NULL || length == 0u || (address & (line - 1u)) != 0u ||
        (length & (line - 1u)) != 0u || length > UINTPTR_MAX - address ||
        (operation != AV_CACHE_CLEAN && operation != AV_CACHE_INVALIDATE &&
         operation != AV_CACHE_CLEAN_INVALIDATE)) return false;
    const uintptr_t end = address + length;
    av_dsb();
    for (; address < end; address += line) {
        switch (operation) {
        case AV_CACHE_CLEAN:
            __asm volatile("mcr p15, 0, %0, c7, c10, 1" :: "r"(address) : "memory");
            break;
        case AV_CACHE_INVALIDATE:
            __asm volatile("mcr p15, 0, %0, c7, c6, 1" :: "r"(address) : "memory");
            break;
        case AV_CACHE_CLEAN_INVALIDATE:
            __asm volatile("mcr p15, 0, %0, c7, c14, 1" :: "r"(address) : "memory");
            break;
        }
    }
    av_dsb();
    return true;
}
