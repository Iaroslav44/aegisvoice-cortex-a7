#ifndef AV_GICV2_H
#define AV_GICV2_H

#include <stdbool.h>
#include <stdint.h>

/* Configure only a disabled SPI. BSP owns GIC security groups, distributor and
 * CPU interface initialization and PPI/timer setup. target_mask is the actual
 * CPU target bit read/verified by the BSP, not an assumed logical CPU index.
 * priority is the raw 8-bit GIC register encoding, unlike the unshifted
 * configMAX_API_CALL_INTERRUPT_PRIORITY. Caller serializes register RMW. */
bool av_gicv2_configure_spi(uintptr_t distributor, unsigned irq,
                          uint8_t target_mask, uint8_t priority, bool edge);
bool av_gicv2_enable_spi(uintptr_t distributor, unsigned irq, bool enable);

/* FreeRTOS portASM.S owns IAR/EOIR. Never acknowledge or EOI again in the C
 * peripheral dispatcher. Handle the full IAR value exactly as port requires. */

#endif
