#ifndef AV_SECURE_H
#define AV_SECURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "av/config.h"
#include "av/status.h"

#define AV_HANDSHAKE_MAX 136u
#define AV_SECURE_HEADER 24u
#define AV_SECURE_TAG 16u
#define AV_SECURE_OVERHEAD (AV_SECURE_HEADER + AV_SECURE_TAG)

typedef av_status (*av_rng_fn)(void *context, uint8_t *output, size_t length);
typedef enum { AV_ROLE_INITIATOR = 1, AV_ROLE_RESPONDER = 2 } av_role;
typedef struct {
    uint8_t secret[64];
    uint8_t public_key[32];
} av_identity;

/* Storage is public to permit static allocation, not direct manipulation.
 * Never copy a live session: copying TX counters can reuse an AEAD nonce.
 * Serialize all calls for one session in its owning RTOS task.
 * identity and RNG context must outlive the session. */
typedef struct {
    const av_identity *identity;
    av_rng_fn random;
    void *random_context;
    av_role role;
    unsigned state;
    uint8_t peer_public[32];
    uint8_t ephemeral_secret[32];
    uint8_t hello[104];
    uint8_t transcript[64];
    uint8_t tx_key[32], rx_key[32];
    uint8_t tx_prefix[16], rx_prefix[16];
    uint8_t confirm_i[32], confirm_r[32];
    uint64_t tx_sequence, rx_highest, rx_window;
    bool rx_started;
} av_secure_session;

/* Seed import is for on-device provisioning/storage, never a firmware constant.
 * Both constructors consume/wipe the seed; RNG must be a cryptographic RNG. */
av_status av_identity_from_seed(av_identity *identity, uint8_t seed[32]);
av_status av_identity_generate(av_identity *identity, av_rng_fn random, void *context);
void av_identity_destroy(av_identity *identity);
av_status av_secure_init(av_secure_session *session, const av_identity *identity,
                         const uint8_t peer_public[32], av_role role,
                         av_rng_fn random, void *context);
/* I: NULL -> M1; R: M1 -> M2; I: M2 -> M3; R: M3 -> M4; I: M4 -> empty.
 * Authenticate the peer's public key out of band before configuring this API.
 * Invalid received messages abort the handshake; reconnect with fresh entropy.
 * Input/output buffers must not overlap each other or session/identity storage. */
av_status av_secure_handshake(av_secure_session *session,
                              const uint8_t *input, size_t input_length,
                              uint8_t *output, size_t output_capacity,
                              size_t *output_length);
bool av_secure_ready(const av_secure_session *session);
void av_secure_destroy(av_secure_session *session);
/* For seal/open, plain and packet must be disjoint, except that
 * plain == packet + AV_SECURE_HEADER permits an in-place payload transform.
 * No other partial overlap is supported. Buffers and output scalars must not
 * overlap each other (apart from that exception), session or identity storage.
 * The caller owns plaintext lifetime and must wipe it when no longer needed. */
av_status av_secure_seal(av_secure_session *session, uint32_t timestamp,
                         const uint8_t *plain, size_t plain_length,
                         uint8_t *packet, size_t packet_capacity,
                         size_t *packet_length);
/* On rejection, plaintext and RX replay state are unchanged. With both output
 * scalars non-NULL, plain_length and timestamp are reset to zero on rejection.
 * Successful in-place decryption consumes the packet's ciphertext payload. */
av_status av_secure_open(av_secure_session *session,
                         const uint8_t *packet, size_t packet_length,
                         uint8_t *plain, size_t plain_capacity,
                         size_t *plain_length, uint32_t *timestamp);

#endif
