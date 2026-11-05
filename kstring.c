/* kstring.c - minimal string and memory helpers */

#include "kstring.h"

u32 kstrlen(const char *s)
{
    u32 n = 0;                              /* Counter */
    while (s[n] != '\0') {                  /* Walk until the terminating 0 byte */
        n++;
    }
    return n;
}

int kstrcmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {            /* Advance while both match and 'a' isn't finished */
        a++;
        b++;
    }
    /* Compare as UNSIGNED chars so bytes >= 0x80 order consistently */
    return (int)(u8)*a - (int)(u8)*b;           /* 0 when both reached '\0' together */
}

/* The copies use the CPU's string instructions (REP STOSB / REP MOVSB) rather than a C loop.
 * Reason: GCC could recognise a C loop as `memset` and compile it into a CALL to memset,
 * which would make memset call itself forever. Inline asm cannot be rewritten that way.
 * The Direction Flag (DF) is assumed clear, as the ABI requires (isr_common does `cld`). */

void *memset(void *dst, int value, size_t n)
{
    void *ret = dst;                        /* memset returns the original pointer */
    __asm__ volatile ("rep stosb"           /* Store AL at [EDI], EDI++, ECX times */
        : "+D"(dst), "+c"(n)                /* EDI = destination, ECX = count */
        : "a"((u8)value)                    /* AL = the byte to store */
        : "memory");
    return ret;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    void *ret = dst;
    __asm__ volatile ("rep movsb"           /* Copy byte [ESI] -> [EDI], ECX times */
        : "+D"(dst), "+S"(src), "+c"(n)
        :
        : "memory");
    return ret;
}

void *memmove(void *dst, const void *src, size_t n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = n; i != 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const u8 *p1 = (const u8 *)a;
    const u8 *p2 = (const u8 *)b;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }
    return 0;
}
