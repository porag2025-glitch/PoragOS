/* kstring.h - minimal string and memory helpers (we have no C library). */
#ifndef KSTRING_H
#define KSTRING_H

#include "types.h"

u32 kstrlen(const char *s);                     /* Characters before the NUL */
int kstrcmp(const char *a, const char *b);      /* 0 if equal, <0 or >0 otherwise */

/* These four use the STANDARD names on purpose. Even with -ffreestanding, GCC may emit
 * calls to memset/memcpy/memmove/memcmp on its own (struct copies, loop-to-memset
 * optimisation at -O2). Defining them ourselves guarantees those calls link. */
void *memset(void *dst, int value, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int   memcmp(const void *a, const void *b, size_t n);

#endif
