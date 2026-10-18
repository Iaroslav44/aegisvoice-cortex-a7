#ifndef AV_OBJECT_CHECK_STDIO_H
#define AV_OBJECT_CHECK_STDIO_H
/* Declaration only for object compilation without a target libc/sysroot.
 * Never include this directory in a real firmware build. */
#include <stddef.h>
int snprintf(char *restrict destination, size_t capacity,
             const char *restrict format, ...);
#endif
