#include "av/audio.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

/* Keep the straightforward, unfused IMA recurrence as a test oracle. In
 * particular, do not replace these separately truncated terms with a multiply
 * and shift: that gives different results for many of the odd step sizes. */
static const int32_t reference_steps[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
    143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
    4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493,
    10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
    24623, 27086, 29794, 32767
};

static void reference_apply(uint8_t code, int32_t *predictor, int32_t *index)
{
    static const int32_t deltas[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
    const int32_t step = reference_steps[*index];
    int32_t difference = step / 8;
    if ((code & 4u) != 0u) {
        difference += step;
    }
    if ((code & 2u) != 0u) {
        difference += step / 2;
    }
    if ((code & 1u) != 0u) {
        difference += step / 4;
    }
    *predictor += (code & 8u) != 0u ? -difference : difference;
    if (*predictor > INT16_MAX) {
        *predictor = INT16_MAX;
    } else if (*predictor < INT16_MIN) {
        *predictor = INT16_MIN;
    }
    *index += deltas[code & 7u];
    if (*index < 0) {
        *index = 0;
    } else if (*index > 88) {
        *index = 88;
    }
}

static void reference_encode(const int16_t pcm[AV_PCM_SAMPLES],
                             uint8_t encoded[AV_ADPCM_BYTES])
{
    int32_t predictor = pcm[0];
    int32_t index = 0;
    const uint16_t first = (uint16_t)pcm[0];
    size_t sample;
    memset(encoded, 0, AV_ADPCM_BYTES);
    encoded[0] = (uint8_t)first;
    encoded[1] = (uint8_t)(first >> 8);
    for (sample = 1; sample < AV_PCM_SAMPLES; ++sample) {
        int32_t difference = (int32_t)pcm[sample] - predictor;
        int32_t step = reference_steps[index];
        uint8_t code = 0;
        const size_t nibble = sample - 1u;
        if (difference < 0) {
            code = 8u;
            difference = -difference;
        }
        if (difference >= step) {
            code |= 4u;
            difference -= step;
        }
        step /= 2;
        if (difference >= step) {
            code |= 2u;
            difference -= step;
        }
        step /= 2;
        if (difference >= step) {
            code |= 1u;
        }
        reference_apply(code, &predictor, &index);
        encoded[4u + nibble / 2u] |= (uint8_t)(code << ((nibble & 1u) * 4u));
    }
}

static int test_all_decoder_states(void)
{
    const int16_t starts[] = { INT16_MIN, INT16_MIN + 1, -1, 0, 1,
                              INT16_MAX - 1, INT16_MAX };
    uint8_t encoded[AV_ADPCM_BYTES];
    int16_t pcm[AV_PCM_SAMPLES];
    unsigned int initial_index;
    unsigned int code;
    size_t value;
    size_t count;
    size_t sample;
    for (initial_index = 0; initial_index < 89u; ++initial_index) {
        for (code = 0; code < 16u; ++code) {
            for (value = 0; value < sizeof(starts) / sizeof(starts[0]); ++value) {
                const uint16_t first = (uint16_t)starts[value];
                int32_t predictor = starts[value];
                int32_t index = (int32_t)initial_index;
                memset(encoded, (int)(code | (code << 4)), sizeof(encoded));
                encoded[0] = (uint8_t)first;
                encoded[1] = (uint8_t)(first >> 8);
                encoded[2] = (uint8_t)initial_index;
                encoded[3] = 0;
                encoded[AV_ADPCM_BYTES - 1u] = (uint8_t)code;
                CHECK(av_audio_decode(encoded, sizeof(encoded), pcm,
                                      AV_PCM_SAMPLES, &count) == AV_OK);
                CHECK(count == AV_PCM_SAMPLES && pcm[0] == starts[value]);
                for (sample = 1; sample < AV_PCM_SAMPLES; ++sample) {
                    reference_apply((uint8_t)code, &predictor, &index);
                    CHECK(pcm[sample] == predictor);
                }
            }
        }
    }
    return 0;
}

static int test_constant_frames(void)
{
    const int16_t values[] = { 0, 12345, -23456, INT16_MIN, INT16_MAX };
    int16_t pcm[AV_PCM_SAMPLES];
    int16_t decoded[AV_PCM_SAMPLES];
    uint8_t encoded[AV_ADPCM_BYTES];
    size_t count;
    size_t written;
    size_t value;
    size_t i;
    for (value = 0; value < sizeof(values) / sizeof(values[0]); ++value) {
        for (i = 0; i < AV_PCM_SAMPLES; ++i) {
            pcm[i] = values[value];
        }
        CHECK(av_audio_encode(pcm, AV_PCM_SAMPLES, encoded, sizeof(encoded), &written) == AV_OK);
        CHECK(written == AV_ADPCM_BYTES);
        CHECK(encoded[2] == 0u && encoded[3] == 0u);
        for (i = 4; i < sizeof(encoded); ++i) {
            CHECK(encoded[i] == 0u);
        }
        CHECK(av_audio_decode(encoded, written, decoded, AV_PCM_SAMPLES, &count) == AV_OK);
        CHECK(count == AV_PCM_SAMPLES);
        CHECK(memcmp(pcm, decoded, sizeof(pcm)) == 0);
    }
    return 0;
}

static int test_known_nibbles_and_saturation(void)
{
    uint8_t encoded[AV_ADPCM_BYTES] = { 0 };
    int16_t pcm[AV_PCM_SAMPLES];
    size_t count;
    size_t i;
    /* IMA reference arithmetic: initial step 7, +11 then step 16, -30. */
    encoded[4] = 0xf7u;
    CHECK(av_audio_decode(encoded, sizeof(encoded), pcm, AV_PCM_SAMPLES, &count) == AV_OK);
    CHECK(pcm[0] == 0 && pcm[1] == 11 && pcm[2] == -19);
    pcm[0] = 0;
    pcm[1] = 11;
    for (i = 2; i < AV_PCM_SAMPLES; ++i) {
        pcm[i] = -19;
    }
    CHECK(av_audio_encode(pcm, AV_PCM_SAMPLES, encoded, sizeof(encoded), &count) == AV_OK);
    CHECK(encoded[0] == 0u && encoded[1] == 0u && encoded[4] == 0xf7u);
    memset(encoded, 0x77, sizeof(encoded));
    encoded[0] = 0xffu;
    encoded[1] = 0x7fu;
    encoded[2] = 88u;
    encoded[3] = 0;
    encoded[AV_ADPCM_BYTES - 1u] = 7u;
    CHECK(av_audio_decode(encoded, sizeof(encoded), pcm, AV_PCM_SAMPLES, &count) == AV_OK);
    for (i = 0; i < AV_PCM_SAMPLES; ++i) {
        CHECK(pcm[i] == INT16_MAX);
    }
    memset(encoded, 0xff, sizeof(encoded));
    encoded[0] = 0;
    encoded[1] = 0x80u;
    encoded[2] = 88u;
    encoded[3] = 0;
    encoded[AV_ADPCM_BYTES - 1u] = 15u;
    CHECK(av_audio_decode(encoded, sizeof(encoded), pcm, AV_PCM_SAMPLES, &count) == AV_OK);
    for (i = 0; i < AV_PCM_SAMPLES; ++i) {
        CHECK(pcm[i] == INT16_MIN);
    }
    return 0;
}

static int test_signal_and_independent_frames(void)
{
    int16_t pcm[AV_PCM_SAMPLES];
    int16_t decoded[AV_PCM_SAMPLES];
    uint8_t encoded[AV_ADPCM_BYTES];
    uint8_t again[AV_ADPCM_BYTES];
    size_t count;
    size_t written;
    size_t i;
    for (i = 0; i < AV_PCM_SAMPLES; ++i) {
        const int32_t phase = (int32_t)(i % 64u);
        pcm[i] = (int16_t)((phase < 32 ? phase : 63 - phase) * 500 - 8000);
    }
    CHECK(av_audio_encode(pcm, AV_PCM_SAMPLES, encoded, sizeof(encoded), &written) == AV_OK);
    CHECK(av_audio_decode(encoded, written, decoded, AV_PCM_SAMPLES, &count) == AV_OK);
    CHECK(decoded[0] == pcm[0]);
    for (i = 32; i < AV_PCM_SAMPLES; ++i) {
        const int32_t difference = (int32_t)decoded[i] - pcm[i];
        CHECK(difference > -1000 && difference < 1000);
    }
    CHECK(av_audio_encode(pcm, AV_PCM_SAMPLES, again, sizeof(again), &written) == AV_OK);
    CHECK(memcmp(encoded, again, sizeof(encoded)) == 0);
    return 0;
}

static int test_invalid_and_random_frames(void)
{
    struct { uint8_t before; uint8_t bytes[AV_ADPCM_BYTES]; uint8_t after; } encoded;
    struct { int16_t before; int16_t samples[AV_PCM_SAMPLES]; int16_t after; } pcm;
    uint8_t reference[AV_ADPCM_BYTES];
    uint32_t random_state = UINT32_C(0x12345678);
    size_t length = 999;
    size_t i;
    unsigned int iteration;
    memset(&encoded, 0, sizeof(encoded));
    memset(&pcm, 0, sizeof(pcm));
    encoded.before = 0x5au;
    encoded.after = 0xa5u;
    pcm.before = 1234;
    pcm.after = -5678;
    CHECK(av_audio_encode(NULL, AV_PCM_SAMPLES, encoded.bytes, AV_ADPCM_BYTES, &length) == AV_ERR_ARGUMENT);
    CHECK(length == 0);
    CHECK(av_audio_encode(pcm.samples, AV_PCM_SAMPLES - 1u, encoded.bytes, AV_ADPCM_BYTES, &length) == AV_ERR_SIZE);
    CHECK(av_audio_encode(pcm.samples, AV_PCM_SAMPLES, encoded.bytes, AV_ADPCM_BYTES - 1u, &length) == AV_ERR_SIZE);
    CHECK(av_audio_encode(pcm.samples, AV_PCM_SAMPLES, encoded.bytes, AV_ADPCM_BYTES, NULL) == AV_ERR_ARGUMENT);
    CHECK(av_audio_decode(encoded.bytes, AV_ADPCM_BYTES - 1u, pcm.samples, AV_PCM_SAMPLES, &length) == AV_ERR_SIZE);
    CHECK(av_audio_decode(encoded.bytes, AV_ADPCM_BYTES, pcm.samples, AV_PCM_SAMPLES - 1u, &length) == AV_ERR_SIZE);
    CHECK(av_audio_decode(NULL, AV_ADPCM_BYTES, pcm.samples, AV_PCM_SAMPLES, &length) == AV_ERR_ARGUMENT);
    encoded.bytes[2] = 89u;
    CHECK(av_audio_decode(encoded.bytes, AV_ADPCM_BYTES, pcm.samples, AV_PCM_SAMPLES, &length) == AV_ERR_FORMAT);
    CHECK(length == 0);
    encoded.bytes[2] = 0;
    encoded.bytes[3] = 1;
    CHECK(av_audio_decode(encoded.bytes, AV_ADPCM_BYTES, pcm.samples, AV_PCM_SAMPLES, &length) == AV_ERR_FORMAT);
    encoded.bytes[3] = 0;
    encoded.bytes[AV_ADPCM_BYTES - 1u] = 0x10u;
    CHECK(av_audio_decode(encoded.bytes, AV_ADPCM_BYTES, pcm.samples, AV_PCM_SAMPLES, &length) == AV_ERR_FORMAT);
    for (iteration = 0; iteration < 512u; ++iteration) {
        int32_t reference_predictor;
        int32_t reference_index = 0;
        const int32_t scale = (int32_t)(UINT32_C(1) << (iteration % 16u));
        for (i = 0; i < AV_PCM_SAMPLES; ++i) {
            random_state = random_state * UINT32_C(1664525) + UINT32_C(1013904223);
            /* Include low-level audio as well as full-scale noise, so the
             * reference comparison exercises the entire step-table range. */
            pcm.samples[i] = (int16_t)(((int32_t)(random_state >> 16) - 32768) / scale);
        }
        reference_predictor = pcm.samples[0];
        reference_encode(pcm.samples, reference);
        /* Every byte must be assigned, including the reserved byte/padding. */
        memset(encoded.bytes, 0xa5, sizeof(encoded.bytes));
        CHECK(av_audio_encode(pcm.samples, AV_PCM_SAMPLES, encoded.bytes, AV_ADPCM_BYTES, &length) == AV_OK);
        CHECK(memcmp(encoded.bytes, reference, sizeof(reference)) == 0);
        CHECK((encoded.bytes[AV_ADPCM_BYTES - 1u] & 0xf0u) == 0u);
        CHECK(av_audio_decode(encoded.bytes, length, pcm.samples, AV_PCM_SAMPLES, &length) == AV_OK);
        CHECK(pcm.samples[0] == reference_predictor);
        for (i = 1; i < AV_PCM_SAMPLES; ++i) {
            const size_t nibble = i - 1u;
            const uint8_t code = (uint8_t)(((unsigned int)reference[4u + nibble / 2u] >>
                                           ((nibble & 1u) * 4u)) & 0x0fu);
            reference_apply(code, &reference_predictor, &reference_index);
            CHECK(pcm.samples[i] == reference_predictor);
        }
        CHECK(encoded.before == 0x5au && encoded.after == 0xa5u);
        CHECK(pcm.before == 1234 && pcm.after == -5678);
    }
    return 0;
}

int main(void)
{
    CHECK(test_all_decoder_states() == 0);
    CHECK(test_constant_frames() == 0);
    CHECK(test_known_nibbles_and_saturation() == 0);
    CHECK(test_signal_and_independent_frames() == 0);
    CHECK(test_invalid_and_random_frames() == 0);
    puts("audio: all checks passed");
    return 0;
}
