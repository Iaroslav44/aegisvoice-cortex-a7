# Protocol v1

Two endpoints, initiator I and responder R, communicate through an untrusted relay. The relay can observe metadata, delay, reorder or discard traffic. It receives no plaintext or secret keys. Availability, traffic analysis and physical extraction of device secrets are outside the protocol's protection.

## Keys and ownership

Each device provisions a unique Ed25519 seed and an authenticated peer public key. The firmware contains no shared private-key constant. `av_identity_from_seed()` erases its input seed. A board must provide protected storage, validated entropy, secure boot/update and debug-access policy.

A session has one task owner. Do not copy it, restore RAM snapshots, reset a counter under an existing key or call it concurrently. Identity objects outlive their sessions. Destroy the session on disconnect; a new handshake requires fresh X25519 randomness. Handshake failure or session destruction erases session secrets; identity destruction is separate.

Monocypher 4.0.2 supplies X25519, standard SHA-512 Ed25519 and XChaCha20-Poly1305. HKDF-SHA512 derives directional material. Reject an all-zero X25519 shared secret. Algorithms and public keys are public; each directional traffic key is held by both endpoints.

## Handshake

Messages are byte arrays, not C structures. Strings below are ASCII without NUL; `||` means concatenation. Public keys are 32 bytes and signatures 64 bytes. Ed25519 signs the 64-byte digest as a normal message, not Ed25519ph.

`H(t)` is 8 bytes: ASCII `AVHS`, version `1`, type `t`, sender role, reserved `0`. Role is `1` for types 1/3 and `2` for types 2/4.

```text
D1 = SHA512("AegisVoice/v1/initiator-hello" || PK_I || PK_R || H(1) || E_I)
M1 = H(1) || E_I || Ed25519Sign(SK_I, D1)                    [104 bytes]
T  = SHA512("AegisVoice/v1/transcript" || PK_I || PK_R || M1 || H(2) || E_R)
Z  = X25519(local ephemeral private, remote ephemeral public)
KM = HKDF-SHA512(ikm=Z, salt=T,
                info="AegisVoice/v1/key-schedule", length=160)
```

`PK_I/PK_R` are pinned identity public keys; `E_I/E_R` are ephemeral X25519 public keys. The transcript binds both identities, roles, ephemeral keys and M1.

| KM bytes, inclusive | Purpose |
|---|---|
| 0..31 / 32..63 | Audio keys I to R / R to I |
| 64..79 / 80..95 | Nonce prefixes I to R / R to I |
| 96..127 / 128..159 | Confirmation keys K_I / K_R |

```text
C(t) = first32(HMAC-SHA512(K_I if t == 3 else K_R,
                          "AegisVoice/v1/confirmation" || H(t) || T))
M2 = H(2) || E_R || Ed25519Sign(SK_R, T) || C(2)             [136 bytes]
M3 = H(3) || C(3)                                           [40 bytes]
M4 = H(4) || C(4)                                           [40 bytes]
```

I sends M1; R verifies it and sends M2; I verifies M2 and sends M3; R verifies M3 and sends M4. R becomes ready after generating M4, I after verifying M4. Ephemeral secrets and confirmation material are then erased.

Invalid messages or RNG failure close and erase the session. Insufficient output capacity permits retry with a larger buffer. The caller supplies ordered delivery and an absolute timeout; firmware uses five seconds. Lost handshake messages require a fresh connection/session. Do not replace identity pins with unauthenticated input.

## Audio packets

Audio is mono PCM16 at 16 kHz: 320 samples per independent 20 ms ADPCM frame. The 164-byte payload contains a little-endian signed predictor, an index byte (0..88), a zero reserved byte and 319 IMA nibbles, low nibble first. The final high nibble is zero.

All multibyte packet header fields are big-endian.

| Offset | Bytes | Value |
|---|---:|---|
| 0 | 2 | ASCII AV |
| 2 | 1 | Version 1 |
| 3 | 1 | Audio type 0x20 |
| 4 | 1 | Sender role 1 or 2 |
| 5 | 1 | Codec 1 |
| 6 | 2 | Reserved zero |
| 8 | 8 | Sequence |
| 16 | 4 | Sample timestamp |
| 20 | 2 | Payload length L |
| 22 | 2 | Reserved zero |
| 24 | L | Ciphertext |
| 24+L | 16 | Authentication tag |

The 24-byte header is authenticated as AAD. `nonce = direction_prefix[16] || BE64(sequence)`. Sequence starts at zero and advances after successful encryption. `UINT64_MAX` returns `AV_ERR_EXHAUSTED`; establish a fresh session. Timestamp advances by 320 modulo 2^32 and is not a nonce.

Normal packets are 204 bytes. The crypto API accepts payloads 0..192 bytes; the audio API requires exactly 164. A 64-bit replay bitmap accepts unseen sequences within the window. Authentication must succeed before advancing that window; a forged high sequence cannot advance it.

`av_secure_open()` authenticates before writing plaintext. Failure leaves the output buffer unchanged and clears output length/timestamp. Device playback produces silence on failure with valid output arguments. Exact in-place operation at `packet + AV_SECURE_HEADER` is supported; other buffer overlaps are prohibited.

## Transport

GATT UUIDs are defined in [gatt.h](../include/av/gatt.h). The BSP supplies service registration, MTU negotiation and flow control.

Each fragment contains `A7 01 | message_id LE16 | total LE16 | offset LE16 | payload`. ATT value capacity is 20..244 bytes; total message size is at most 256. Reassembly handles one ordered message at a time and expires after 1000 ms without progress. Malformed or out-of-order input resets assembly. Fragment headers are unauthenticated; every assembled message must pass protocol validation.

The relay forwards complete control/audio traffic in both directions. It must not decode audio or receive private identity/session keys. Bluetooth pairing does not authenticate the remote endpoint.

References: [RFC 7748](https://www.rfc-editor.org/rfc/rfc7748), [RFC 8032](https://www.rfc-editor.org/rfc/rfc8032), [RFC 5869](https://www.rfc-editor.org/rfc/rfc5869), [Monocypher AEAD](https://monocypher.org/manual/aead).
