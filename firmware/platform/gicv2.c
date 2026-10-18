#include "gicv2.h"
#include "armv7a.h"

#define GICD_TYPER       0x004u
#define GICD_ISENABLER   0x100u
#define GICD_ICENABLER   0x180u
#define GICD_IPRIORITYR  0x400u
#define GICD_ITARGETSR   0x800u
#define GICD_ICFGR       0xc00u

static volatile uint32_t *reg32(uintptr_t base, unsigned offset)
{
    return (volatile uint32_t *)(base + offset);
}

static bool valid_spi(uintptr_t distributor, unsigned irq)
{
    if (distributor == 0u || (distributor & 0xfffu) != 0u) return false;
    unsigned count = ((*reg32(distributor, GICD_TYPER) & 31u) + 1u) * 32u;
    return irq >= 32u && irq < count && irq < 1020u;
}

bool av_gicv2_configure_spi(uintptr_t distributor, unsigned irq,
                          uint8_t target_mask, uint8_t priority, bool edge)
{
    if (!valid_spi(distributor, irq) || target_mask == 0u) return false;
    if ((*reg32(distributor, GICD_ISENABLER + 4u * (irq / 32u)) &
         (UINT32_C(1) << (irq % 32u))) != 0u) return false;
    volatile uint8_t *priorities = (volatile uint8_t *)(distributor + GICD_IPRIORITYR);
    volatile uint8_t *targets = (volatile uint8_t *)(distributor + GICD_ITARGETSR);
    volatile uint32_t *configuration = reg32(distributor, GICD_ICFGR + 4u * (irq / 16u));
    unsigned shift = (irq % 16u) * 2u + 1u;
    uint32_t value = *configuration;
    value &= ~(UINT32_C(1) << shift);
    if (edge) value |= UINT32_C(1) << shift;
    *configuration = value;
    priorities[irq] = priority;
    targets[irq] = target_mask;
    av_dsb();
    av_isb();
    return true;
}

bool av_gicv2_enable_spi(uintptr_t distributor, unsigned irq, bool enable)
{
    if (!valid_spi(distributor, irq)) return false;
    unsigned offset = enable ? GICD_ISENABLER : GICD_ICENABLER;
    *reg32(distributor, offset + 4u * (irq / 32u)) = UINT32_C(1) << (irq % 32u);
    av_dsb();
    av_isb();
    return true;
}
