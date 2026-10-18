#include "av/audio.h"

#include <limits.h>

_Static_assert(AV_PCM_SAMPLES == 320u, "Wire format requires 320 PCM samples");
_Static_assert(AV_ADPCM_BYTES == 164u, "Wire format requires 164 ADPCM bytes");

static const uint16_t step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
    143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
    4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493,
    10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385,
    24623, 27086, 29794, 32767
};

static const int8_t index_delta[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

static int32_t clamp_predictor(int32_t value)
{
    if (value > INT16_MAX) {
        return INT16_MAX;
    }
    if (value < INT16_MIN) {
        return INT16_MIN;
    }
    return value;
}

static void update_state(uint8_t code, int32_t difference,
                         int32_t *predictor, int32_t *index)
{
    int32_t next_index = *index + index_delta[code & 7u];
    *predictor = clamp_predictor(*predictor + (((code & 8u) != 0u) ?
                                             -difference : difference));
    if (next_index < 0) {
        next_index = 0;
    } else if (next_index > 88) {
        next_index = 88;
    }
    *index = next_index;
}

static inline void apply_nibble(uint8_t code, int32_t *predictor, int32_t *index)
{
    const int32_t step = step_table[*index];
    int32_t difference = step >> 3;
    if ((code & 4u) != 0u) {
        difference += step;
    }
    if ((code & 2u) != 0u) {
        difference += step >> 1;
    }
    if ((code & 1u) != 0u) {
        difference += step >> 2;
    }
    update_state(code, difference, predictor, index);
}

static inline uint8_t encode_sample(int16_t sample, int32_t *predictor, int32_t *index)
{
    int32_t difference = (int32_t)sample - *predictor;
    int32_t step = step_table[*index];
    int32_t reconstructed = step >> 3;
    uint8_t code = 0;
    if (difference < 0) {
        code = 8u;
        difference = -difference;
    }
    /* Accumulate the decoder's exact per-bit truncated terms while selecting
     * the code. Multiplying step by (2 * magnitude + 1) then shifting is not
     * equivalent when step is odd, and would make encoder/decoder diverge. */
    if (difference >= step) {
        code |= 4u;
        difference -= step;
        reconstructed += step;
    }
    step >>= 1;
    if (difference >= step) {
        code |= 2u;
        difference -= step;
        reconstructed += step;
    }
    step >>= 1;
    if (difference >= step) {
        code |= 1u;
        reconstructed += step;
    }
    update_state(code, reconstructed, predictor, index);
    return code;
}

av_status av_audio_encode(const int16_t pcm[AV_PCM_SAMPLES], size_t sample_count,
                          uint8_t encoded[AV_ADPCM_BYTES], size_t encoded_capacity,
                          size_t *encoded_len)
{
    int32_t predictor;
    int32_t index = 0;
    uint16_t first;
    size_t sample;
    if (encoded_len == NULL) {
        return AV_ERR_ARGUMENT;
    }
    *encoded_len = 0;
    if (pcm == NULL || encoded == NULL) {
        return AV_ERR_ARGUMENT;
    }
    if (sample_count != AV_PCM_SAMPLES || encoded_capacity < AV_ADPCM_BYTES) {
        return AV_ERR_SIZE;
    }
    predictor = pcm[0];
    first = (uint16_t)pcm[0];
    encoded[0] = (uint8_t)first;
    encoded[1] = (uint8_t)(first >> 8);
    encoded[2] = (uint8_t)index;
    encoded[3] = 0;
    for (sample = 1; sample + 1u < AV_PCM_SAMPLES; sample += 2u) {
        const uint8_t low = encode_sample(pcm[sample], &predictor, &index);
        const uint8_t high = encode_sample(pcm[sample + 1u], &predictor, &index);
        encoded[4u + (sample - 1u) / 2u] = (uint8_t)(low | (uint8_t)(high << 4));
    }
    /* 319 codes leave one low nibble; assigning it also clears padding. */
    encoded[AV_ADPCM_BYTES - 1u] = encode_sample(pcm[sample], &predictor, &index);
    *encoded_len = AV_ADPCM_BYTES;
    return AV_OK;
}

av_status av_audio_decode(const uint8_t encoded[AV_ADPCM_BYTES], size_t encoded_len,
                          int16_t pcm[AV_PCM_SAMPLES], size_t pcm_capacity_samples,
                          size_t *sample_count)
{
    int32_t predictor;
    int32_t index;
    uint16_t first;
    size_t sample;
    if (sample_count == NULL) {
        return AV_ERR_ARGUMENT;
    }
    *sample_count = 0;
    if (encoded == NULL || pcm == NULL) {
        return AV_ERR_ARGUMENT;
    }
    if (encoded_len != AV_ADPCM_BYTES || pcm_capacity_samples < AV_PCM_SAMPLES) {
        return AV_ERR_SIZE;
    }
    if (encoded[2] > 88u || encoded[3] != 0u ||
        (encoded[AV_ADPCM_BYTES - 1u] & 0xf0u) != 0u) {
        return AV_ERR_FORMAT;
    }
    first = (uint16_t)((uint16_t)encoded[0] | ((uint16_t)encoded[1] << 8));
    predictor = first <= INT16_MAX ? (int32_t)first : (int32_t)first - 65536;
    index = encoded[2];
    pcm[0] = (int16_t)predictor;
    for (sample = 1; sample + 1u < AV_PCM_SAMPLES; sample += 2u) {
        const uint8_t codes = encoded[4u + (sample - 1u) / 2u];
        apply_nibble((uint8_t)(codes & 0x0fu), &predictor, &index);
        pcm[sample] = (int16_t)predictor;
        apply_nibble((uint8_t)(codes >> 4), &predictor, &index);
        pcm[sample + 1u] = (int16_t)predictor;
    }
    apply_nibble(encoded[AV_ADPCM_BYTES - 1u], &predictor, &index);
    pcm[sample] = (int16_t)predictor;
    *sample_count = AV_PCM_SAMPLES;
    return AV_OK;
}
