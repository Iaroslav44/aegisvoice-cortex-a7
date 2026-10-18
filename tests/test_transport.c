#include "av/transport.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int test_all_sizes(void)
{
    const size_t mtus[] = { 20u, 21u, 64u, 244u };
    av_fragmenter tx;
    av_reassembly rx;
    uint8_t input[AV_MAX_PACKET];
    uint8_t fragment[AV_ATT_PAYLOAD_MAX];
    uint8_t output[AV_MAX_PACKET];
    size_t fragment_len;
    size_t complete_len;
    size_t mtu;
    size_t total;
    size_t i;
    for (i = 0; i < sizeof(input); ++i) {
        input[i] = (uint8_t)(i ^ 0xa5u);
    }
    for (mtu = 0; mtu < sizeof(mtus) / sizeof(mtus[0]); ++mtu) {
        for (total = 1; total <= AV_MAX_PACKET; ++total) {
            size_t received = 0;
            av_reassembly_init(&rx);
            CHECK(av_fragmenter_init(&tx, UINT16_MAX, input, total) == AV_OK);
            while (received < total) {
                CHECK(av_fragmenter_next(&tx, mtus[mtu], fragment, sizeof(fragment), &fragment_len) == AV_OK);
                CHECK(fragment_len <= mtus[mtu]);
                received += fragment_len - AV_FRAGMENT_HEADER_BYTES;
                CHECK(av_reassembly_accept(&rx, fragment, fragment_len, (uint32_t)received,
                                           output, sizeof(output), &complete_len) == AV_OK);
                CHECK(complete_len == (received == total ? total : 0u));
            }
            CHECK(memcmp(input, output, total) == 0);
            CHECK(!rx.active);
            CHECK(av_fragmenter_next(&tx, mtus[mtu], fragment, sizeof(fragment), &fragment_len) == AV_ERR_EMPTY);
            CHECK(fragment_len == 0);
        }
    }
    return 0;
}

static int test_order_timeout_and_errors(void)
{
    av_fragmenter tx;
    av_reassembly rx;
    uint8_t input[AV_MAX_PACKET] = { 0 };
    uint8_t first[20];
    uint8_t second[20];
    uint8_t changed[20];
    uint8_t output[AV_MAX_PACKET];
    size_t first_len;
    size_t second_len;
    size_t complete_len;
    const uint32_t start_ms = UINT32_MAX - UINT32_C(499);
    av_reassembly_init(&rx);
    CHECK(av_fragmenter_init(&tx, 42u, input, 24u) == AV_OK);
    CHECK(av_fragmenter_next(&tx, 19u, first, sizeof(first), &first_len) == AV_ERR_SIZE);
    CHECK(av_fragmenter_next(&tx, 245u, first, sizeof(first), &first_len) == AV_ERR_SIZE);
    CHECK(av_fragmenter_next(&tx, 20u, first, 19u, &first_len) == AV_ERR_SIZE);
    CHECK(tx.offset == 0);
    CHECK(av_fragmenter_next(&tx, 20u, first, sizeof(first), &first_len) == AV_OK);
    CHECK(av_fragmenter_next(&tx, 20u, second, sizeof(second), &second_len) == AV_OK);
    CHECK(av_reassembly_accept(&rx, second, second_len, 0, output, sizeof(output), &complete_len) == AV_ERR_STATE);
    CHECK(!rx.active && complete_len == 0);
    CHECK(av_reassembly_accept(&rx, first, first_len, 0, output, sizeof(output), &complete_len) == AV_OK);
    CHECK(rx.active && complete_len == 0);
    CHECK(av_reassembly_accept(&rx, first, first_len, 1, output, sizeof(output), &complete_len) == AV_ERR_STATE);
    CHECK(!rx.active);
    CHECK(av_reassembly_accept(&rx, first, first_len, 0, output, sizeof(output), &complete_len) == AV_OK);
    memcpy(changed, second, sizeof(changed));
    changed[2] ^= 1u;
    CHECK(av_reassembly_accept(&rx, changed, second_len, 1, output, sizeof(output), &complete_len) == AV_ERR_STATE);
    CHECK(!rx.active);
    CHECK(av_reassembly_accept(&rx, first, first_len, start_ms, output, sizeof(output), &complete_len) == AV_OK);
    CHECK(!av_reassembly_expire(&rx, 499u));
    CHECK(av_reassembly_expire(&rx, 500u));
    CHECK(!av_reassembly_expire(&rx, 501u));
    CHECK(av_reassembly_accept(&rx, second, second_len, 501u, output, sizeof(output), &complete_len) == AV_ERR_STATE);
    CHECK(av_reassembly_accept(&rx, first, first_len, 100u, output, sizeof(output), &complete_len) == AV_OK);
    CHECK(av_reassembly_accept(&rx, second, second_len, 1100u, output, sizeof(output), &complete_len) == AV_ERR_STATE);
    CHECK(av_reassembly_accept(&rx, first, first_len, 1200u, output, sizeof(output), &complete_len) == AV_OK);
    CHECK(av_reassembly_accept(&rx, second, second_len, 1201u, output, 23u, &complete_len) == AV_ERR_SIZE);
    CHECK(!rx.active && complete_len == 0);
    CHECK(av_reassembly_accept(&rx, first, first_len, 1300u, output, sizeof(output), &complete_len) == AV_OK);
    CHECK(av_reassembly_accept(&rx, second, second_len, 1301u, output, sizeof(output), &complete_len) == AV_OK);
    CHECK(complete_len == 24u && memcmp(input, output, complete_len) == 0);
    memcpy(changed, first, sizeof(changed));
    changed[0] = 0;
    CHECK(av_reassembly_accept(&rx, changed, first_len, 0, output, sizeof(output), &complete_len) == AV_ERR_FORMAT);
    memcpy(changed, first, sizeof(changed));
    changed[4] = 1;
    changed[5] = 1; /* total 257 */
    CHECK(av_reassembly_accept(&rx, changed, first_len, 0, output, sizeof(output), &complete_len) == AV_ERR_FORMAT);
    changed[4] = 0;
    changed[5] = 0;
    CHECK(av_reassembly_accept(&rx, changed, first_len, 0, output, sizeof(output), &complete_len) == AV_ERR_FORMAT);
    memcpy(changed, first, sizeof(changed));
    changed[6] = 0xffu;
    changed[7] = 0xffu;
    CHECK(av_reassembly_accept(&rx, changed, first_len, 0, output, sizeof(output), &complete_len) == AV_ERR_FORMAT);
    CHECK(av_reassembly_accept(&rx, first, AV_FRAGMENT_HEADER_BYTES, 0, output, sizeof(output), &complete_len) == AV_ERR_SIZE);
    CHECK(av_fragmenter_init(&tx, 0, input, 0) == AV_ERR_SIZE);
    CHECK(av_fragmenter_next(&tx, 20, first, sizeof(first), &first_len) == AV_ERR_EMPTY);
    CHECK(av_fragmenter_init(&tx, 0, input, AV_MAX_PACKET + 1u) == AV_ERR_SIZE);
    return 0;
}

static int test_malformed_fuzz(void)
{
    av_reassembly rx;
    uint8_t fragment[AV_ATT_PAYLOAD_MAX + 1u];
    struct { uint8_t before; uint8_t data[AV_MAX_PACKET]; uint8_t after; } output;
    uint32_t random_state = UINT32_C(0x7b341fa9);
    size_t complete_len;
    unsigned int iteration;
    size_t i;
    memset(&output, 0xa5, sizeof(output));
    av_reassembly_init(&rx);
    for (iteration = 0; iteration < 10000u; ++iteration) {
        const size_t len = iteration % (sizeof(fragment) + 1u);
        av_status status;
        for (i = 0; i < sizeof(fragment); ++i) {
            random_state = random_state * UINT32_C(1664525) + UINT32_C(1013904223);
            fragment[i] = (uint8_t)(random_state >> 24);
        }
        if ((iteration & 1u) != 0u) {
            fragment[0] = 0xa7u;
            fragment[1] = 1u;
        }
        status = av_reassembly_accept(&rx, fragment, len, iteration,
                                      output.data, sizeof(output.data), &complete_len);
        CHECK(status == AV_OK || status == AV_ERR_SIZE || status == AV_ERR_FORMAT || status == AV_ERR_STATE);
        CHECK(complete_len <= AV_MAX_PACKET);
        CHECK(output.before == 0xa5u && output.after == 0xa5u);
        CHECK(!rx.active || (rx.next_offset <= rx.total && rx.total <= AV_MAX_PACKET));
        if (status != AV_OK) {
            CHECK(complete_len == 0u && !rx.active);
        }
    }
    return 0;
}

int main(void)
{
    CHECK(test_all_sizes() == 0);
    CHECK(test_order_timeout_and_errors() == 0);
    CHECK(test_malformed_fuzz() == 0);
    puts("transport: all checks passed");
    return 0;
}
