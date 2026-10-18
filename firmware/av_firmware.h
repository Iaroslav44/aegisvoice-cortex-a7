#ifndef AV_FIRMWARE_H
#define AV_FIRMWARE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "av/config.h"

/* Callbacks run only in the media task; no concurrent access to session keys.
 * Capacity in *length on entry, actual bytes on success. Return false on any
 * failure, never partially publish plaintext. Control/encrypted output contains
 * public handshake data/ciphertext only, never keys or PCM. decrypt_frame fills
 * the entire PCM frame on success and returns silence on failure. All
 * operations are bounded. */
typedef struct {
    void *context;
    void (*reset_session)(void *context);
    bool (*begin_session)(void *context, uint8_t *packet, size_t *length);
    bool (*on_control)(void *context, const uint8_t *packet, size_t length,
                       uint8_t *response, size_t *response_length,
                       bool *authenticated);
    bool (*encrypt_frame)(void *context, const int16_t pcm[AV_PCM_SAMPLES],
                          uint8_t *packet, size_t *length);
    bool (*decrypt_frame)(void *context, const uint8_t *packet, size_t length,
                          int16_t pcm[AV_PCM_SAMPLES]);
    bool (*public_identity)(void *context, uint8_t identity[32]);
} av_firmware_app_ops;

typedef struct {
    void *context;
    /* Single caller: Bluetooth task. Epoch MUST change on every connection,
     * disconnection or route change, including reconnects between two polls.
     * No wrap/reuse during boot. Notify runtime on every link/route change.
     * This driver must make link reset observable
     * before any packet for the new connection can be received/transmitted. */
    bool (*link_state)(void *context, uint32_t *epoch, bool *connected);
    /* Complete reassembled packets, no partial ATT fragments. Empty: length=0.
     * The BSP performs ATT fragmentation/credits/MTU negotiation. Both calls
     * are nonblocking/bounded. Notify runtime after publishing RX data. send()
     * must accept/copy a whole packet or reject it (counted as a drop). */
    bool (*receive)(void *context, bool *control, uint8_t *packet, size_t *length);
    bool (*send)(void *context, uint32_t epoch, bool control,
                 const uint8_t *packet, size_t length);
    /* Completed capture buffers remain CPU-owned until release. Claim handles
     * DMA synchronization before returning a stable pointer; release cannot
     * block; release erases captured samples before returning ownership to DMA.
     * DMA must never automatically recycle a queued buffer. */
    bool (*capture_claim)(void *context, uint8_t buffer, const int16_t **pcm);
    void (*capture_release)(void *context, uint8_t buffer);
    /* Atomically validate actual link epoch then copy all samples before
     * return. No borrowed stack pointer. Reset must
     * cancel queued playback/DMA, mute output and erase device PCM buffers. */
    bool (*playback_copy)(void *context, uint32_t epoch,
                          const int16_t pcm[AV_PCM_SAMPLES]);
    void (*playback_reset)(void *context);
    /* Nonblocking, bounded USB CDC reads/writes; diagnostics only. Notify
     * runtime after publishing received bytes. Writes copy bytes on success. */
    size_t (*usb_read)(void *context, uint8_t *bytes, size_t capacity);
    bool (*usb_write)(void *context, const uint8_t *bytes, size_t length);
    bool (*watchdog_feed)(void *context);
    /* Must mute/stop DMA and clear sensitive RAM before reset. Never return.
     * Also called from stack overflow hook: use a known-good exception stack,
     * no RTOS API, mutex, printf or dynamic allocation inside this path. */
    void (*fatal)(void *context, const char *reason);
} av_firmware_hal_ops;

/* Call once before starting scheduler; IRQ producers remain disabled until
 * successful return. App/HAL contexts must remain alive for the whole boot. */
bool av_firmware_create(const av_firmware_app_ops *app,
                        const av_firmware_hal_ops *hal);
/* ISR owns buffer on false; BSP must discard/release it without queuing again.
 * Call only at a FreeRTOS-API-safe GIC priority. It requests an ISR-end yield. */
bool av_firmware_capture_from_isr(uint8_t buffer);
/* BSP publishes RX bytes/packets or link state before signalling. Notifications
 * coalesce and remain latched across check-to-sleep races; no one-notification-
 * per-packet assumption. Task variants are for thread context only, ISR
 * variants require a FreeRTOS-API-safe GIC priority and request ISR-end yield.
 * Before create succeeds these calls do nothing. Bounded 50 ms heartbeat waits
 * provide health checks; notifications provide prompt service. */
void av_firmware_bluetooth_notify(void);
void av_firmware_bluetooth_notify_from_isr(void);
void av_firmware_usb_notify(void);
void av_firmware_usb_notify_from_isr(void);

#endif
