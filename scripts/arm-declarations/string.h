#ifndef AV_OBJECT_CHECK_STRING_H
#define AV_OBJECT_CHECK_STRING_H
/* ISO C declarations only for a freestanding object compile without a target
 * libc/sysroot. Never include this directory in a real firmware build. No
 * implementations are supplied; final linking still requires the BSP libc. */
#include <stddef.h>
void *memcpy(void *restrict destination, const void *restrict source, size_t size);
void *memset(void *destination, int value, size_t size);
int memcmp(const void *left, const void *right, size_t size);
int strcmp(const char *left, const char *right);
size_t strlen(const char *string);
#endif
