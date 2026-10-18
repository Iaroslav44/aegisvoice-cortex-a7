#include "av/secure.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include <string.h>

enum { ST_NEW = 1, ST_WAIT_RESPONSE, ST_WAIT_FINISH, ST_WAIT_ACK, ST_READY };
static const uint8_t zeros[32] = {0};
static const uint8_t hello_domain[] = "AegisVoice/v1/initiator-hello";
static const uint8_t transcript_domain[] = "AegisVoice/v1/transcript";
static const uint8_t schedule_domain[] = "AegisVoice/v1/key-schedule";

_Static_assert(AV_MAX_PAYLOAD + AV_SECURE_OVERHEAD <= AV_MAX_PACKET,
               "encrypted audio must fit transport packet");

static void put16(uint8_t *p, uint16_t x)
{ p[0] = (uint8_t)(x >> 8); p[1] = (uint8_t)x; }
static void put32(uint8_t *p, uint32_t x)
{ for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(x >> (24u - 8u*i)); }
static void put64(uint8_t *p, uint64_t x)
{ for (unsigned i = 0; i < 8; ++i) p[i] = (uint8_t)(x >> (56u - 8u*i)); }
static uint16_t get16(const uint8_t *p)
{ return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static uint32_t get32(const uint8_t *p)
{ uint32_t x = 0; for (unsigned i = 0; i < 4; ++i) x = (x << 8) | p[i]; return x; }
static uint64_t get64(const uint8_t *p)
{ uint64_t x = 0; for (unsigned i = 0; i < 8; ++i) x = (x << 8) | p[i]; return x; }

void av_identity_destroy(av_identity *id)
{ if (id != NULL) crypto_wipe(id, sizeof(*id)); }

av_status av_identity_from_seed(av_identity *id, uint8_t seed[32])
{
    if (id == NULL || seed == NULL) return AV_ERR_ARGUMENT;
    if (crypto_verify32(seed, zeros) == 0) {
        crypto_wipe(seed, 32); av_identity_destroy(id); return AV_ERR_RANDOM;
    }
    crypto_ed25519_key_pair(id->secret, id->public_key, seed);
    return AV_OK;
}

av_status av_identity_generate(av_identity *id, av_rng_fn random, void *context)
{
    uint8_t seed[32] = {0};
    av_status result;
    if (id == NULL || random == NULL) return AV_ERR_ARGUMENT;
    av_identity_destroy(id);
    result = random(context, seed, sizeof(seed));
    if (result != AV_OK) { crypto_wipe(seed, sizeof(seed)); return AV_ERR_RANDOM; }
    return av_identity_from_seed(id, seed);
}

void av_secure_destroy(av_secure_session *s)
{ if (s != NULL) crypto_wipe(s, sizeof(*s)); }

av_status av_secure_init(av_secure_session *s, const av_identity *id,
                         const uint8_t peer[32], av_role role,
                         av_rng_fn random, void *context)
{
    if (s == NULL || id == NULL || peer == NULL || random == NULL ||
        (role != AV_ROLE_INITIATOR && role != AV_ROLE_RESPONDER)) return AV_ERR_ARGUMENT;
    av_secure_destroy(s);
    if (crypto_verify32(peer, zeros) == 0 || crypto_verify32(id->public_key, zeros) == 0 ||
        crypto_verify32(id->secret + 32, id->public_key) != 0 ||
        crypto_verify32(id->public_key, peer) == 0) return AV_ERR_ARGUMENT;
    s->identity = id; s->random = random; s->random_context = context;
    s->role = role; s->state = ST_NEW;
    memcpy(s->peer_public, peer, 32);
    return AV_OK;
}

bool av_secure_ready(const av_secure_session *s)
{ return s != NULL && s->state == ST_READY; }

static av_status abort_handshake(av_secure_session *s, av_status reason)
{ av_secure_destroy(s); return reason; }

static void hs_header(uint8_t *p, uint8_t type)
{
    memcpy(p, "AVHS", 4); p[4] = 1; p[5] = type;
    p[6] = (type == 1 || type == 3) ? AV_ROLE_INITIATOR : AV_ROLE_RESPONDER;
    p[7] = 0;
}

static bool hs_header_valid(const uint8_t *p, uint8_t type)
{
    uint8_t expected[8]; hs_header(expected, type);
    return memcmp(p, expected, sizeof(expected)) == 0;
}

static void hash_identities(crypto_sha512_ctx *hash, const av_secure_session *s)
{
    const uint8_t *initiator = s->role == AV_ROLE_INITIATOR ? s->identity->public_key : s->peer_public;
    const uint8_t *responder = s->role == AV_ROLE_RESPONDER ? s->identity->public_key : s->peer_public;
    crypto_sha512_update(hash, initiator, 32);
    crypto_sha512_update(hash, responder, 32);
}

static void hello_hash(uint8_t digest[64], const av_secure_session *s, const uint8_t *hello)
{
    crypto_sha512_ctx hash;
    crypto_sha512_init(&hash);
    crypto_sha512_update(&hash, hello_domain, sizeof(hello_domain) - 1);
    hash_identities(&hash, s);
    crypto_sha512_update(&hash, hello, 40);
    crypto_sha512_final(&hash, digest);
}

static void transcript_hash(av_secure_session *s, const uint8_t *response)
{
    crypto_sha512_ctx hash;
    crypto_sha512_init(&hash);
    crypto_sha512_update(&hash, transcript_domain, sizeof(transcript_domain) - 1);
    hash_identities(&hash, s);
    crypto_sha512_update(&hash, s->hello, sizeof(s->hello));
    crypto_sha512_update(&hash, response, 40);
    crypto_sha512_final(&hash, s->transcript);
}

static av_status ephemeral_generate(av_secure_session *s, uint8_t public_key[32])
{
    if (s->random(s->random_context, s->ephemeral_secret, 32) != AV_OK ||
        crypto_verify32(s->ephemeral_secret, zeros) == 0) return AV_ERR_RANDOM;
    crypto_x25519_public_key(public_key, s->ephemeral_secret);
    return AV_OK;
}

static av_status derive_keys(av_secure_session *s, const uint8_t peer_ephemeral[32])
{
    uint8_t shared[32], material[160];
    crypto_x25519(shared, s->ephemeral_secret, peer_ephemeral);
    crypto_wipe(s->ephemeral_secret, sizeof(s->ephemeral_secret));
    if (crypto_verify32(shared, zeros) == 0) {
        crypto_wipe(shared, sizeof(shared)); return AV_ERR_AUTH;
    }
    crypto_sha512_hkdf(material, sizeof(material), shared, sizeof(shared),
                       s->transcript, sizeof(s->transcript), schedule_domain,
                       sizeof(schedule_domain) - 1);
    if (s->role == AV_ROLE_INITIATOR) {
        memcpy(s->tx_key, material, 32); memcpy(s->rx_key, material + 32, 32);
        memcpy(s->tx_prefix, material + 64, 16); memcpy(s->rx_prefix, material + 80, 16);
    } else {
        memcpy(s->rx_key, material, 32); memcpy(s->tx_key, material + 32, 32);
        memcpy(s->rx_prefix, material + 64, 16); memcpy(s->tx_prefix, material + 80, 16);
    }
    memcpy(s->confirm_i, material + 96, 32);
    memcpy(s->confirm_r, material + 128, 32);
    crypto_wipe(shared, sizeof(shared)); crypto_wipe(material, sizeof(material));
    return AV_OK;
}

static void confirmation(uint8_t output[32], const av_secure_session *s, uint8_t type)
{
    uint8_t full[64], header[8];
    static const uint8_t domain[] = "AegisVoice/v1/confirmation";
    crypto_sha512_hmac_ctx hash;
    hs_header(header, type);
    crypto_sha512_hmac_init(&hash, type == 3 ? s->confirm_i : s->confirm_r, 32);
    crypto_sha512_hmac_update(&hash, domain, sizeof(domain) - 1);
    crypto_sha512_hmac_update(&hash, header, sizeof(header));
    crypto_sha512_hmac_update(&hash, s->transcript, sizeof(s->transcript));
    crypto_sha512_hmac_final(&hash, full);
    memcpy(output, full, 32); crypto_wipe(full, sizeof(full));
}

static void finish_handshake(av_secure_session *s)
{
    crypto_wipe(s->confirm_i, sizeof(s->confirm_i));
    crypto_wipe(s->confirm_r, sizeof(s->confirm_r));
    crypto_wipe(s->hello, sizeof(s->hello));
    crypto_wipe(s->transcript, sizeof(s->transcript));
    s->state = ST_READY;
}

av_status av_secure_handshake(av_secure_session *s,
                              const uint8_t *in, size_t in_len,
                              uint8_t *out, size_t out_cap, size_t *out_len)
{
    uint8_t digest[64], expected[32];
    av_status result;
    size_t need = 0, expected_input = 0;
    uint8_t type = 0;
    if (out_len == NULL) return AV_ERR_ARGUMENT;
    *out_len = 0;
    if (s == NULL || (in_len != 0 && in == NULL)) return AV_ERR_ARGUMENT;
    if (s->state == ST_NEW && s->role == AV_ROLE_INITIATOR) need = 104;
    else if (s->state == ST_NEW && s->role == AV_ROLE_RESPONDER) {
        expected_input = 104; type = 1; need = 136;
    } else if (s->state == ST_WAIT_RESPONSE) {
        expected_input = 136; type = 2; need = 40;
    } else if (s->state == ST_WAIT_FINISH) {
        expected_input = 40; type = 3; need = 40;
    } else if (s->state == ST_WAIT_ACK) {
        expected_input = 40; type = 4;
    } else return AV_ERR_STATE;
    if (in_len != expected_input) return abort_handshake(s, AV_ERR_SIZE);
    if (type != 0 && !hs_header_valid(in, type)) return abort_handshake(s, AV_ERR_FORMAT);
    if (need > out_cap || (need != 0 && out == NULL)) return AV_ERR_SIZE;
    if (type == 0) {
        hs_header(s->hello, 1);
        result = ephemeral_generate(s, s->hello + 8);
        if (result != AV_OK) return abort_handshake(s, result);
        hello_hash(digest, s, s->hello);
        crypto_ed25519_sign(s->hello + 40, s->identity->secret, digest, sizeof(digest));
        memcpy(out, s->hello, 104); s->state = ST_WAIT_RESPONSE;
    } else if (type == 1) {
        hello_hash(digest, s, in);
        if (crypto_ed25519_check(in + 40, s->peer_public, digest, sizeof(digest)) != 0)
            return abort_handshake(s, AV_ERR_AUTH);
        memcpy(s->hello, in, 104); hs_header(out, 2);
        result = ephemeral_generate(s, out + 8);
        if (result != AV_OK) return abort_handshake(s, result);
        transcript_hash(s, out);
        crypto_ed25519_sign(out + 40, s->identity->secret, s->transcript, 64);
        result = derive_keys(s, s->hello + 8);
        if (result != AV_OK) return abort_handshake(s, result);
        confirmation(out + 104, s, 2); s->state = ST_WAIT_FINISH;
    } else if (type == 2) {
        transcript_hash(s, in);
        if (crypto_ed25519_check(in + 40, s->peer_public, s->transcript, 64) != 0)
            return abort_handshake(s, AV_ERR_AUTH);
        result = derive_keys(s, in + 8);
        if (result != AV_OK) return abort_handshake(s, result);
        confirmation(expected, s, 2);
        if (crypto_verify32(expected, in + 104) != 0) return abort_handshake(s, AV_ERR_AUTH);
        hs_header(out, 3); confirmation(out + 8, s, 3); s->state = ST_WAIT_ACK;
    } else {
        confirmation(expected, s, type);
        if (crypto_verify32(expected, in + 8) != 0) return abort_handshake(s, AV_ERR_AUTH);
        if (type == 3) { hs_header(out, 4); confirmation(out + 8, s, 4); }
        finish_handshake(s);
    }
    crypto_wipe(digest, sizeof(digest)); crypto_wipe(expected, sizeof(expected));
    *out_len = need;
    return AV_OK;
}

av_status av_secure_seal(av_secure_session *s, uint32_t timestamp,
                         const uint8_t *plain, size_t plain_len,
                         uint8_t *packet, size_t packet_cap, size_t *packet_len)
{
    uint8_t nonce[24];
    if (packet_len == NULL) return AV_ERR_ARGUMENT;
    *packet_len = 0;
    if (s == NULL || packet == NULL || (plain == NULL && plain_len != 0)) return AV_ERR_ARGUMENT;
    if (!av_secure_ready(s)) return AV_ERR_STATE;
    if (plain_len > AV_MAX_PAYLOAD || packet_cap < plain_len + AV_SECURE_OVERHEAD) return AV_ERR_SIZE;
    if (s->tx_sequence == UINT64_MAX) return AV_ERR_EXHAUSTED;
    memset(packet, 0, AV_SECURE_HEADER);
    packet[0] = 'A'; packet[1] = 'V'; packet[2] = 1; packet[3] = 0x20;
    packet[4] = (uint8_t)s->role; packet[5] = 1; /* Independently framed IMA ADPCM. */
    put64(packet + 8, s->tx_sequence); put32(packet + 16, timestamp);
    put16(packet + 20, (uint16_t)plain_len);
    memcpy(nonce, s->tx_prefix, 16); put64(nonce + 16, s->tx_sequence);
    crypto_aead_lock(packet + AV_SECURE_HEADER, packet + AV_SECURE_HEADER + plain_len,
                     s->tx_key, nonce, packet, AV_SECURE_HEADER, plain, plain_len);
    ++s->tx_sequence;
    *packet_len = AV_SECURE_OVERHEAD + plain_len;
    crypto_wipe(nonce, sizeof(nonce));
    return AV_OK;
}

av_status av_secure_open(av_secure_session *s,
                         const uint8_t *packet, size_t packet_len,
                         uint8_t *plain, size_t plain_cap,
                         size_t *plain_len, uint32_t *timestamp)
{
    uint8_t nonce[24];
    uint64_t sequence, delta;
    size_t length;
    if (plain_len == NULL || timestamp == NULL) return AV_ERR_ARGUMENT;
    *plain_len = 0; *timestamp = 0;
    if (s == NULL || packet == NULL || plain == NULL) return AV_ERR_ARGUMENT;
    if (!av_secure_ready(s)) return AV_ERR_STATE;
    if (packet_len < AV_SECURE_OVERHEAD || packet_len > AV_MAX_PAYLOAD + AV_SECURE_OVERHEAD)
        return AV_ERR_SIZE;
    if (packet[0] != 'A' || packet[1] != 'V' || packet[2] != 1 || packet[3] != 0x20 ||
        packet[4] != (uint8_t)(s->role == AV_ROLE_INITIATOR ? AV_ROLE_RESPONDER : AV_ROLE_INITIATOR) ||
        packet[5] != 1 || packet[6] != 0 || packet[7] != 0 || packet[22] != 0 || packet[23] != 0)
        return AV_ERR_FORMAT;
    length = get16(packet + 20);
    if (length > AV_MAX_PAYLOAD || packet_len != length + AV_SECURE_OVERHEAD || plain_cap < length)
        return AV_ERR_SIZE;
    sequence = get64(packet + 8);
    if (sequence == UINT64_MAX) return AV_ERR_EXHAUSTED;
    if (s->rx_started && sequence <= s->rx_highest) {
        delta = s->rx_highest - sequence;
        if (delta >= 64 || (s->rx_window & (UINT64_C(1) << delta)) != 0) return AV_ERR_REPLAY;
    }
    memcpy(nonce, s->rx_prefix, 16); put64(nonce + 16, sequence);
    /* Monocypher verifies the MAC before writing any plaintext. Decrypt directly
     * to the caller's buffer: no temporary plaintext or second copy is needed.
     * Keep this guarantee when changing crypto backends (see test_secure). */
    if (crypto_aead_unlock(plain, packet + AV_SECURE_HEADER + length, s->rx_key, nonce,
                           packet, AV_SECURE_HEADER, packet + AV_SECURE_HEADER, length) != 0) {
        crypto_wipe(nonce, sizeof(nonce));
        return AV_ERR_AUTH;
    }
    /* Only authenticated frames may advance or populate the replay window. */
    if (!s->rx_started) { s->rx_highest = sequence; s->rx_window = 1; s->rx_started = true; }
    else if (sequence > s->rx_highest) {
        delta = sequence - s->rx_highest;
        s->rx_window = delta >= 64 ? 1 : (s->rx_window << delta) | 1;
        s->rx_highest = sequence;
    } else s->rx_window |= UINT64_C(1) << (s->rx_highest - sequence);
    *plain_len = length; *timestamp = get32(packet + 16);
    crypto_wipe(nonce, sizeof(nonce));
    return AV_OK;
}
