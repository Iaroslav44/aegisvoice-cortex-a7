#include "random.h"
#include <limits.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

av_status av_host_random(void *context, uint8_t *out, size_t length)
{
    (void)context;
    if (out == NULL || length == 0u) return AV_ERR_ARGUMENT;
#ifdef _WIN32
    if (length > ULONG_MAX) return AV_ERR_SIZE;
    return BCryptGenRandom(NULL, out, (ULONG)length,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? AV_OK : AV_ERR_RANDOM;
#else
    int fd = open("/dev/urandom", O_RDONLY);
    size_t done = 0u;
    if (fd < 0) return AV_ERR_RANDOM;
    while (done < length) {
        ssize_t got = read(fd, out + done, length - done);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) { (void)close(fd); return AV_ERR_RANDOM; }
        done += (size_t)got;
    }
    (void)close(fd);
    return AV_OK;
#endif
}
