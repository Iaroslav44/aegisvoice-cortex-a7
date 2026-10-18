#include "av/secure.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); return 1; } } while (0)

/* This deliberately deterministic generator is ONLY a test fixture. */
typedef struct { uint64_t state; bool fail; bool zeros; } test_random;
static av_status random_bytes(void *context, uint8_t *out, size_t length)
{
    test_random *r = context;
    if (r->fail) { if (length != 0) out[0] = 0x42; return AV_ERR_IO; }
    for (size_t i = 0; i < length; ++i) {
        r->state ^= r->state << 13; r->state ^= r->state >> 7; r->state ^= r->state << 17;
        out[i] = r->zeros ? 0 : (uint8_t)r->state;
    }
    return AV_OK;
}

typedef struct {
    av_identity alice, bob;
    av_secure_session a, b;
    test_random ra, rb;
} fixture;

static int fixture_init(fixture *f)
{
    memset(f, 0, sizeof(*f));
    f->ra.state = UINT64_C(0x123456789abcdef1); f->rb.state = UINT64_C(0xfedcba9876543211);
    CHECK(av_identity_generate(&f->alice, random_bytes, &f->ra) == AV_OK);
    CHECK(av_identity_generate(&f->bob, random_bytes, &f->rb) == AV_OK);
    CHECK(av_secure_init(&f->a, &f->alice, f->bob.public_key, AV_ROLE_INITIATOR, random_bytes, &f->ra) == AV_OK);
    CHECK(av_secure_init(&f->b, &f->bob, f->alice.public_key, AV_ROLE_RESPONDER, random_bytes, &f->rb) == AV_OK);
    return 0;
}

static int fixture_connect(fixture *f)
{
    uint8_t m1[AV_HANDSHAKE_MAX], m2[AV_HANDSHAKE_MAX]; size_t n1, n2;
    CHECK(fixture_init(f) == 0);
    CHECK(av_secure_handshake(&f->a, NULL, 0, m1, sizeof(m1), &n1) == AV_OK && n1 == 104);
    CHECK(av_secure_handshake(&f->b, m1, n1, m2, sizeof(m2), &n2) == AV_OK && n2 == 136);
    CHECK(av_secure_handshake(&f->a, m2, n2, m1, sizeof(m1), &n1) == AV_OK && n1 == 40);
    CHECK(!av_secure_ready(&f->a) && !av_secure_ready(&f->b));
    CHECK(av_secure_handshake(&f->b, m1, n1, m2, sizeof(m2), &n2) == AV_OK && n2 == 40);
    CHECK(av_secure_ready(&f->b) && !av_secure_ready(&f->a));
    CHECK(av_secure_handshake(&f->a, m2, n2, NULL, 0, &n1) == AV_OK && n1 == 0);
    CHECK(av_secure_ready(&f->a));
    CHECK(memcmp(f->a.tx_key, f->b.rx_key, 32) == 0);
    CHECK(memcmp(f->a.rx_key, f->b.tx_key, 32) == 0);
    CHECK(memcmp(f->a.tx_key, f->a.rx_key, 32) != 0);
    return 0;
}

static int all_zero(const void *data, size_t length)
{
    const uint8_t *p = data; uint8_t sum = 0;
    for (size_t i = 0; i < length; ++i) sum |= p[i];
    return sum == 0;
}

static unsigned hex_digit(char c)
{ return (unsigned)(c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10); }

static void from_hex(uint8_t *out, const char *hex)
{
    size_t count = strlen(hex)/2;
    for (size_t i = 0; i < count; ++i) {
        out[i] = (uint8_t)((hex_digit(hex[2*i]) << 4) | hex_digit(hex[2*i + 1]));
    }
}

static int primitive_vectors(void)
{
    uint8_t seed[32], pub[32], secret[64], signature[64], expected[64];
    /* RFC 8032 section 7.1, test 1 (empty message). */
    from_hex(seed, "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
    crypto_ed25519_key_pair(secret, pub, seed);
    CHECK(all_zero(seed, sizeof(seed)));
    from_hex(expected, "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    CHECK(memcmp(pub, expected, 32) == 0);
    crypto_ed25519_sign(signature, secret, (const uint8_t *)"", 0);
    from_hex(expected, "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555f"
                       "b8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
    CHECK(memcmp(signature, expected, 64) == 0);
    CHECK(crypto_ed25519_check(signature, pub, (const uint8_t *)"", 0) == 0);
    signature[0] ^= 1;
    CHECK(crypto_ed25519_check(signature, pub, (const uint8_t *)"", 0) != 0);
    /* RFC 7748 section 6.1, Alice public key and shared secret. */
    from_hex(seed, "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a");
    crypto_x25519_public_key(pub, seed);
    from_hex(expected, "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a");
    CHECK(memcmp(pub, expected, 32) == 0);
    from_hex(pub, "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f");
    crypto_x25519(signature, seed, pub);
    from_hex(expected, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742");
    CHECK(memcmp(signature, expected, 32) == 0);
    crypto_wipe(secret, sizeof(secret)); crypto_wipe(seed, sizeof(seed));
    return 0;
}

static int roundtrip_and_bounds(void)
{
    fixture f; uint8_t payload[AV_MAX_PAYLOAD], plain[AV_MAX_PAYLOAD], packet[AV_MAX_PACKET];
    size_t n, m; uint32_t timestamp;
    CHECK(fixture_connect(&f) == 0);
    for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)i;
    for (unsigned direction = 0; direction < 2; ++direction) {
        av_secure_session *tx = direction == 0 ? &f.a : &f.b;
        av_secure_session *rx = direction == 0 ? &f.b : &f.a;
        for (size_t length = 0; length <= AV_MAX_PAYLOAD; ++length) {
            CHECK(av_secure_seal(tx, 0x1234abcd, payload, length, packet, sizeof(packet), &n) == AV_OK);
            CHECK(n == length + AV_SECURE_OVERHEAD);
            CHECK(packet[16] == 0x12 && packet[17] == 0x34 && packet[18] == 0xab && packet[19] == 0xcd);
            memset(plain, 0xa5, sizeof(plain));
            CHECK(av_secure_open(rx, packet, n, plain, length, &m, &timestamp) == AV_OK);
            CHECK(m == length && timestamp == 0x1234abcd && memcmp(payload, plain, m) == 0);
            for (size_t i = length; i < sizeof(plain); ++i) CHECK(plain[i] == 0xa5);
            CHECK(av_secure_open(rx, packet, n, plain, sizeof(plain), &m, &timestamp) == AV_ERR_REPLAY);
        }
    }
    CHECK(av_secure_seal(&f.a, 0, payload, AV_MAX_PAYLOAD + 1, packet, sizeof(packet), &n) == AV_ERR_SIZE);
    uint64_t sequence = f.a.tx_sequence;
    CHECK(av_secure_seal(&f.a, 0, payload, 10, packet, 49, &n) == AV_ERR_SIZE && n == 0);
    CHECK(f.a.tx_sequence == sequence);
    CHECK(av_secure_seal(&f.a, 0, payload, 10, packet, sizeof(packet), &n) == AV_OK);
    CHECK(av_secure_open(&f.b, packet, n, plain, 9, &m, &timestamp) == AV_ERR_SIZE);
    CHECK(av_secure_open(&f.b, packet, n - 1, plain, sizeof(plain), &m, &timestamp) == AV_ERR_SIZE);
    CHECK(av_secure_open(&f.b, packet, n, plain, sizeof(plain), &m, &timestamp) == AV_OK);
    f.a.tx_sequence = UINT64_MAX;
    CHECK(av_secure_seal(&f.a, 0, payload, 10, packet, sizeof(packet), &n) == AV_ERR_EXHAUSTED);
    av_secure_destroy(&f.a); av_secure_destroy(&f.b);
    av_identity_destroy(&f.alice); av_identity_destroy(&f.bob);
    CHECK(all_zero(&f.a, sizeof(f.a)) && all_zero(&f.alice, sizeof(f.alice)));
    return 0;
}

static int rejected_open(av_secure_session *rx, const uint8_t *packet,
                         size_t packet_len, size_t capacity, av_status expected)
{
    uint8_t plain[AV_MAX_PAYLOAD];
    size_t length = SIZE_MAX; uint32_t timestamp = UINT32_MAX;
    const bool started = rx != NULL && rx->rx_started;
    const uint64_t highest = rx != NULL ? rx->rx_highest : 0;
    const uint64_t window = rx != NULL ? rx->rx_window : 0;
    memset(plain, 0xa5, sizeof(plain));
    CHECK(av_secure_open(rx, packet, packet_len, plain, capacity, &length, &timestamp) == expected);
    CHECK(length == 0 && timestamp == 0);
    for (size_t i = 0; i < sizeof(plain); ++i) CHECK(plain[i] == 0xa5);
    if (rx != NULL)
        CHECK(rx->rx_started == started && rx->rx_highest == highest && rx->rx_window == window);
    return 0;
}

static int rejected_packets_preserve_output(void)
{
    fixture f;
    uint8_t packet[AV_MAX_PACKET], altered[AV_MAX_PACKET], plain[AV_MAX_PAYLOAD];
    const uint8_t payload[] = {0x31, 0x41, 0x59}; size_t n, m; uint32_t timestamp;
    CHECK(fixture_connect(&f) == 0);
    CHECK(av_secure_seal(&f.a, 123, payload, sizeof(payload), packet, sizeof(packet), &n) == AV_OK);
    CHECK(rejected_open(NULL, packet, n, sizeof(plain), AV_ERR_ARGUMENT) == 0);
    CHECK(rejected_open(&f.b, NULL, n, sizeof(plain), AV_ERR_ARGUMENT) == 0);
    CHECK(rejected_open(&f.b, packet, AV_SECURE_OVERHEAD - 1, sizeof(plain), AV_ERR_SIZE) == 0);
    CHECK(rejected_open(&f.b, packet, AV_MAX_PAYLOAD + AV_SECURE_OVERHEAD + 1,
                         sizeof(plain), AV_ERR_SIZE) == 0);
    CHECK(rejected_open(&f.b, packet, n, sizeof(payload) - 1, AV_ERR_SIZE) == 0);
    CHECK(rejected_open(&f.b, packet, n - 1, sizeof(plain), AV_ERR_SIZE) == 0);
    memcpy(altered, packet, n); altered[0] ^= 1;
    CHECK(rejected_open(&f.b, altered, n, sizeof(plain), AV_ERR_FORMAT) == 0);
    memcpy(altered, packet, n); memset(altered + 8, 0xff, 8);
    CHECK(rejected_open(&f.b, altered, n, sizeof(plain), AV_ERR_EXHAUSTED) == 0);
    memcpy(altered, packet, n); altered[n - 1] ^= 1;
    CHECK(rejected_open(&f.b, altered, n, sizeof(plain), AV_ERR_AUTH) == 0);
    /* None of the rejected versions consumed the valid frame. */
    CHECK(av_secure_open(&f.b, packet, n, plain, sizeof(plain), &m, &timestamp) == AV_OK);
    CHECK(rejected_open(&f.b, packet, n, sizeof(plain), AV_ERR_REPLAY) == 0);
    av_secure_destroy(&f.b);
    CHECK(rejected_open(&f.b, packet, n, sizeof(plain), AV_ERR_STATE) == 0);
    return 0;
}

static int in_place_payloads(void)
{
    fixture f; uint8_t payload[AV_MAX_PAYLOAD], packet[AV_MAX_PACKET], saved[AV_MAX_PACKET];
    size_t n, m; uint32_t timestamp;
    CHECK(fixture_connect(&f) == 0);
    for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = (uint8_t)i;
    for (unsigned direction = 0; direction < 2; ++direction) {
        av_secure_session *tx = direction == 0 ? &f.a : &f.b;
        av_secure_session *rx = direction == 0 ? &f.b : &f.a;
        for (size_t length = 0; length <= AV_MAX_PAYLOAD; ++length) {
            memset(packet, 0xa5, sizeof(packet));
            memcpy(packet + AV_SECURE_HEADER, payload, length);
            CHECK(av_secure_seal(tx, 0x1234abcd, packet + AV_SECURE_HEADER, length,
                                 packet, length + AV_SECURE_OVERHEAD, &n) == AV_OK);
            CHECK(n == length + AV_SECURE_OVERHEAD);
            for (size_t i = n; i < sizeof(packet); ++i) CHECK(packet[i] == 0xa5);
            packet[n - 1] ^= 1;
            memcpy(saved, packet, sizeof(saved));
            const bool started = rx->rx_started;
            const uint64_t highest = rx->rx_highest, window = rx->rx_window;
            m = SIZE_MAX; timestamp = UINT32_MAX;
            CHECK(av_secure_open(rx, packet, n, packet + AV_SECURE_HEADER, length,
                                 &m, &timestamp) == AV_ERR_AUTH);
            CHECK(m == 0 && timestamp == 0 && memcmp(packet, saved, sizeof(packet)) == 0);
            CHECK(rx->rx_started == started && rx->rx_highest == highest && rx->rx_window == window);
            packet[n - 1] ^= 1;
            memcpy(saved, packet, sizeof(saved));
            CHECK(av_secure_open(rx, packet, n, packet + AV_SECURE_HEADER, length,
                                 &m, &timestamp) == AV_OK);
            CHECK(m == length && timestamp == 0x1234abcd);
            CHECK(memcmp(packet + AV_SECURE_HEADER, payload, length) == 0);
            CHECK(memcmp(packet, saved, AV_SECURE_HEADER) == 0);
            CHECK(memcmp(packet + AV_SECURE_HEADER + length, saved + AV_SECURE_HEADER + length,
                         sizeof(packet) - AV_SECURE_HEADER - length) == 0);
        }
    }
    return 0;
}

static int tamper_and_replay(void)
{
    fixture f; uint8_t packets[70][AV_MAX_PACKET], altered[AV_MAX_PACKET], plain[AV_MAX_PAYLOAD];
    const uint8_t payload[] = {0x31, 0x41, 0x59}; size_t n = 0, m; uint32_t timestamp;
    CHECK(fixture_connect(&f) == 0);
    for (size_t i = 0; i < 70; ++i)
        CHECK(av_secure_seal(&f.a, (uint32_t)i * AV_PCM_SAMPLES, payload, sizeof(payload), packets[i], AV_MAX_PACKET, &n) == AV_OK);
    /* Every byte is authenticated or strictly validated; rejected packets leave
       output and replay state untouched, including a forged huge sequence. */
    for (size_t i = 0; i < n; ++i) {
        memcpy(altered, packets[69], n); altered[i] ^= 0x80; memset(plain, 0xa5, sizeof(plain));
        CHECK(av_secure_open(&f.b, altered, n, plain, sizeof(plain), &m, &timestamp) != AV_OK);
        CHECK(!f.b.rx_started && f.b.rx_window == 0 && m == 0 && timestamp == 0);
        for (size_t j = 0; j < sizeof(plain); ++j) CHECK(plain[j] == 0xa5);
    }
    CHECK(av_secure_open(&f.b, packets[69], n, plain, sizeof(plain), &m, &timestamp) == AV_OK);
    CHECK(av_secure_open(&f.b, packets[5], n, plain, sizeof(plain), &m, &timestamp) == AV_ERR_REPLAY);
    CHECK(av_secure_open(&f.b, packets[6], n, plain, sizeof(plain), &m, &timestamp) == AV_OK);
    CHECK(av_secure_open(&f.b, packets[68], n, plain, sizeof(plain), &m, &timestamp) == AV_OK);
    CHECK(av_secure_open(&f.b, packets[6], n, plain, sizeof(plain), &m, &timestamp) == AV_ERR_REPLAY);
    CHECK(av_secure_open(&f.a, packets[67], n, plain, sizeof(plain), &m, &timestamp) == AV_ERR_FORMAT);
    return 0;
}

static int handshake_failures(void)
{
    fixture f; uint8_t m1[AV_HANDSHAKE_MAX], m2[AV_HANDSHAKE_MAX]; size_t n1, n2;
    /* Mutate each byte in each handshake message in a fresh handshake. */
    const size_t sizes[] = {104,136,40,40};
    for (unsigned step = 0; step < 4; ++step) for (size_t byte = 0; byte < sizes[step]; ++byte) {
        CHECK(fixture_init(&f) == 0);
        CHECK(av_secure_handshake(&f.a, NULL, 0, m1, sizeof(m1), &n1) == AV_OK);
        if (step == 0) {
            m1[byte] ^= 1;
            CHECK(av_secure_handshake(&f.b, m1, n1, m2, sizeof(m2), &n2) != AV_OK);
            CHECK(all_zero(&f.b, sizeof(f.b))); continue;
        }
        CHECK(av_secure_handshake(&f.b, m1, n1, m2, sizeof(m2), &n2) == AV_OK);
        if (step == 1) {
            m2[byte] ^= 1;
            CHECK(av_secure_handshake(&f.a, m2, n2, m1, sizeof(m1), &n1) != AV_OK);
            CHECK(all_zero(&f.a, sizeof(f.a))); continue;
        }
        CHECK(av_secure_handshake(&f.a, m2, n2, m1, sizeof(m1), &n1) == AV_OK);
        if (step == 2) {
            m1[byte] ^= 1;
            CHECK(av_secure_handshake(&f.b, m1, n1, m2, sizeof(m2), &n2) != AV_OK);
            CHECK(all_zero(&f.b, sizeof(f.b))); continue;
        }
        CHECK(av_secure_handshake(&f.b, m1, n1, m2, sizeof(m2), &n2) == AV_OK);
        m2[byte] ^= 1;
        CHECK(av_secure_handshake(&f.a, m2, n2, m1, sizeof(m1), &n1) != AV_OK);
        CHECK(all_zero(&f.a, sizeof(f.a)));
    }
    CHECK(fixture_init(&f) == 0);
    CHECK(av_secure_handshake(&f.a, NULL, 0, m1, 103, &n1) == AV_ERR_SIZE);
    CHECK(av_secure_handshake(&f.a, NULL, 0, m1, sizeof(m1), &n1) == AV_OK);
    CHECK(av_secure_handshake(&f.b, m1, n1 - 1, m2, sizeof(m2), &n2) == AV_ERR_SIZE);
    CHECK(all_zero(&f.b, sizeof(f.b)));
    return 0;
}

static int rng_and_identity_failures(void)
{
    fixture f; uint8_t m1[AV_HANDSHAKE_MAX], zero_seed[32] = {0}; size_t n1;
    CHECK(fixture_init(&f) == 0);
    f.ra.fail = true;
    CHECK(av_secure_handshake(&f.a, NULL, 0, m1, sizeof(m1), &n1) == AV_ERR_RANDOM);
    CHECK(all_zero(&f.a, sizeof(f.a)));
    CHECK(av_identity_generate(&f.alice, random_bytes, &f.ra) == AV_ERR_RANDOM);
    CHECK(all_zero(&f.alice, sizeof(f.alice)));
    CHECK(av_identity_from_seed(&f.alice, zero_seed) == AV_ERR_RANDOM);
    CHECK(fixture_init(&f) == 0);
    f.rb.zeros = true;
    CHECK(av_identity_generate(&f.bob, random_bytes, &f.rb) == AV_ERR_RANDOM);
    CHECK(fixture_init(&f) == 0);
    f.ra.zeros = true;
    CHECK(av_secure_handshake(&f.a, NULL, 0, m1, sizeof(m1), &n1) == AV_ERR_RANDOM);
    CHECK(fixture_init(&f) == 0);
    f.b.peer_public[0] ^= 1;
    CHECK(av_secure_handshake(&f.a, NULL, 0, m1, sizeof(m1), &n1) == AV_OK);
    uint8_t response[AV_HANDSHAKE_MAX]; size_t response_len;
    CHECK(av_secure_handshake(&f.b, m1, n1, response, sizeof(response), &response_len) == AV_ERR_AUTH);
    return 0;
}

static int authenticated_low_order_point(void)
{
    fixture f; uint8_t hello[104], response[AV_HANDSHAKE_MAX], digest[64]; size_t n1, n2;
    static const uint8_t domain[] = "AegisVoice/v1/initiator-hello";
    crypto_sha512_ctx hash;
    CHECK(fixture_init(&f) == 0);
    CHECK(av_secure_handshake(&f.a, NULL, 0, hello, sizeof(hello), &n1) == AV_OK);
    memset(hello + 8, 0, 32); /* A malicious authenticated peer sends a low-order point. */
    crypto_sha512_init(&hash);
    crypto_sha512_update(&hash, domain, sizeof(domain) - 1);
    crypto_sha512_update(&hash, f.alice.public_key, 32);
    crypto_sha512_update(&hash, f.bob.public_key, 32);
    crypto_sha512_update(&hash, hello, 40);
    crypto_sha512_final(&hash, digest);
    crypto_ed25519_sign(hello + 40, f.alice.secret, digest, sizeof(digest));
    CHECK(av_secure_handshake(&f.b, hello, n1, response, sizeof(response), &n2) == AV_ERR_AUTH);
    CHECK(all_zero(&f.b, sizeof(f.b)));
    return 0;
}

int main(void)
{
    CHECK(primitive_vectors() == 0);
    CHECK(roundtrip_and_bounds() == 0);
    CHECK(rejected_packets_preserve_output() == 0);
    CHECK(in_place_payloads() == 0);
    CHECK(tamper_and_replay() == 0);
    CHECK(handshake_failures() == 0);
    CHECK(rng_and_identity_failures() == 0);
    CHECK(authenticated_low_order_point() == 0);
    puts("secure: vectors, mutual authentication, bidirectional AEAD, replay, bounds and failures PASS");
    return 0;
}
