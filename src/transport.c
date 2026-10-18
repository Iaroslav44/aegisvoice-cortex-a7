#include "av/transport.h"

#include <string.h>

_Static_assert(AV_MAX_PACKET <= UINT16_MAX, "Fragment lengths are 16-bit");

static void put_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static uint16_t get_u16(const uint8_t *in)
{
    return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8));
}

av_status av_fragmenter_init(av_fragmenter *tx, uint16_t message_id,
                            const uint8_t *data, size_t len)
{
    if (tx == NULL) {
        return AV_ERR_ARGUMENT;
    }
    tx->total = 0;
    tx->offset = 0;
    if (data == NULL) {
        return AV_ERR_ARGUMENT;
    }
    if (len == 0u || len > AV_MAX_PACKET) {
        return AV_ERR_SIZE;
    }
    memcpy(tx->data, data, len);
    tx->message_id = message_id;
    tx->total = (uint16_t)len;
    return AV_OK;
}

av_status av_fragmenter_next(av_fragmenter *tx, size_t att_payload,
                            uint8_t *out, size_t capacity, size_t *len)
{
    size_t amount;
    if (len == NULL) {
        return AV_ERR_ARGUMENT;
    }
    *len = 0;
    if (tx == NULL || out == NULL) {
        return AV_ERR_ARGUMENT;
    }
    if (att_payload < AV_ATT_PAYLOAD_MIN || att_payload > AV_ATT_PAYLOAD_MAX) {
        return AV_ERR_SIZE;
    }
    if (tx->total > AV_MAX_PACKET || tx->offset > tx->total) {
        return AV_ERR_STATE;
    }
    if (tx->offset == tx->total) {
        return AV_ERR_EMPTY;
    }
    amount = (size_t)tx->total - tx->offset;
    if (amount > att_payload - AV_FRAGMENT_HEADER_BYTES) {
        amount = att_payload - AV_FRAGMENT_HEADER_BYTES;
    }
    if (capacity < AV_FRAGMENT_HEADER_BYTES + amount) {
        return AV_ERR_SIZE;
    }
    out[0] = 0xa7u;
    out[1] = 1u;
    put_u16(out + 2, tx->message_id);
    put_u16(out + 4, tx->total);
    put_u16(out + 6, tx->offset);
    memcpy(out + AV_FRAGMENT_HEADER_BYTES, tx->data + tx->offset, amount);
    tx->offset = (uint16_t)(tx->offset + amount);
    *len = AV_FRAGMENT_HEADER_BYTES + amount;
    return AV_OK;
}

void av_reassembly_init(av_reassembly *rx)
{
    if (rx != NULL) {
        rx->last_ms = 0;
        rx->message_id = 0;
        rx->total = 0;
        rx->next_offset = 0;
        rx->active = false;
    }
}

bool av_reassembly_expire(av_reassembly *rx, uint32_t now_ms)
{
    if (rx != NULL && rx->active &&
        (uint32_t)(now_ms - rx->last_ms) >= AV_REASSEMBLY_TIMEOUT_MS) {
        av_reassembly_init(rx);
        return true;
    }
    return false;
}

av_status av_reassembly_accept(av_reassembly *rx, const uint8_t *fragment,
                              size_t fragment_len, uint32_t now_ms,
                              uint8_t *out, size_t capacity, size_t *complete_len)
{
    uint16_t message_id;
    uint16_t total;
    uint16_t offset;
    size_t amount;
    if (complete_len == NULL) {
        return AV_ERR_ARGUMENT;
    }
    *complete_len = 0;
    if (rx == NULL || fragment == NULL || out == NULL) {
        return AV_ERR_ARGUMENT;
    }
    (void)av_reassembly_expire(rx, now_ms);
    if (fragment_len <= AV_FRAGMENT_HEADER_BYTES || fragment_len > AV_ATT_PAYLOAD_MAX) {
        av_reassembly_init(rx);
        return AV_ERR_SIZE;
    }
    if (fragment[0] != 0xa7u || fragment[1] != 1u) {
        av_reassembly_init(rx);
        return AV_ERR_FORMAT;
    }
    message_id = get_u16(fragment + 2);
    total = get_u16(fragment + 4);
    offset = get_u16(fragment + 6);
    amount = fragment_len - AV_FRAGMENT_HEADER_BYTES;
    if (total == 0u || total > AV_MAX_PACKET || offset >= total ||
        amount > (size_t)total - offset) {
        av_reassembly_init(rx);
        return AV_ERR_FORMAT;
    }
    if ((!rx->active && offset != 0u) ||
        (rx->active && (rx->message_id != message_id || rx->total != total ||
                        rx->next_offset != offset))) {
        av_reassembly_init(rx);
        return AV_ERR_STATE;
    }
    if (!rx->active) {
        rx->active = true;
        rx->message_id = message_id;
        rx->total = total;
        rx->next_offset = 0;
    }
    memcpy(rx->data + offset, fragment + AV_FRAGMENT_HEADER_BYTES, amount);
    rx->next_offset = (uint16_t)(offset + amount);
    rx->last_ms = now_ms;
    if (rx->next_offset == total) {
        if (capacity < total) {
            av_reassembly_init(rx);
            return AV_ERR_SIZE;
        }
        memcpy(out, rx->data, total);
        *complete_len = total;
        av_reassembly_init(rx);
    }
    return AV_OK;
}
