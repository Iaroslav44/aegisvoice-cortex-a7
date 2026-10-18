#ifndef AV_STATUS_H
#define AV_STATUS_H

typedef enum {
    AV_OK = 0,
    AV_ERR_ARGUMENT = -1,
    AV_ERR_SIZE = -2,
    AV_ERR_STATE = -3,
    AV_ERR_AUTH = -4,
    AV_ERR_REPLAY = -5,
    AV_ERR_RANDOM = -6,
    AV_ERR_EMPTY = -8,
    AV_ERR_EXHAUSTED = -9,
    AV_ERR_FORMAT = -10,
    AV_ERR_IO = -11
} av_status;

#endif
