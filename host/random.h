#ifndef AV_HOST_RANDOM_H
#define AV_HOST_RANDOM_H
#include <stddef.h>
#include <stdint.h>
#include "av/status.h"
av_status av_host_random(void *context, uint8_t *out, size_t length);
#endif
