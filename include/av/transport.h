#ifndef AV_TRANSPORT_H
#define AV_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "av/config.h"
#include "av/status.h"

#define AV_FRAGMENT_HEADER_BYTES 8u
#define AV_ATT_PAYLOAD_MIN 20u
#define AV_ATT_PAYLOAD_MAX 244u
#define AV_REASSEMBLY_TIMEOUT_MS 1000u

typedef struct {
    uint8_t data[AV_MAX_PACKET];
    uint16_t message_id;
    uint16_t total;
    uint16_t offset;
} av_fragmenter;

/* Copies the message. Each fragment is <= att_payload, including the header.
 * Wire: A7 01 | message_id LE16 | total LE16 | offset LE16 | payload.
 * att_payload is the GATT ATT value capacity (negotiated ATT MTU minus 3).
 * Input and output buffers must not alias the state object's storage. */
av_status av_fragmenter_init(av_fragmenter *tx, uint16_t message_id,
                            const uint8_t *data, size_t len);
av_status av_fragmenter_next(av_fragmenter *tx, size_t att_payload,
                            uint8_t *out, size_t capacity, size_t *len);

typedef struct {
    uint8_t data[AV_MAX_PACKET];
    uint32_t last_ms;
    uint16_t message_id;
    uint16_t total;
    uint16_t next_offset;
    bool active;
} av_reassembly;

void av_reassembly_init(av_reassembly *rx);
/* Uses modulo-2^32 milliseconds; call at least once per 2^32 ms while active. */
bool av_reassembly_expire(av_reassembly *rx, uint32_t now_ms);
/* Strict ordered single-message assembly. Malformed or out-of-sequence input
 * resets state. A timed-out assembly is discarded before parsing new input.
 * AV_OK with *complete_len == 0 means incomplete; otherwise out holds a packet.
 * AV_ERR_SIZE at completion also discards the assembly. This is NOT an
 * authentication boundary: a completed packet must pass the crypto layer.
 * Caller serializes access. Input/output must not alias rx storage. */
av_status av_reassembly_accept(av_reassembly *rx, const uint8_t *fragment,
                              size_t fragment_len, uint32_t now_ms,
                              uint8_t *out, size_t capacity, size_t *complete_len);

#endif
