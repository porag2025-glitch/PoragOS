/* io.h - x86 port I/O helpers.
 * x86 has a separate 64K "I/O port" address space for device registers.
 * It is reachable only through the IN/OUT instructions, hence inline asm. */
#ifndef IO_H
#define IO_H

#include "types.h"

/* Write one byte to an I/O port. */
static inline void outb(u16 port, u8 value)
{
    /* "a"  = put `value` in AL.
     * "Nd" = put `port` in DX (or use an 8-bit immediate if it is a constant < 256). */
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

/* Read one byte from an I/O port. */
static inline u8 inb(u16 port)
{
    u8 result;                                          /* Will receive AL */
    /* "=a" = output comes from AL; "Nd" = port in DX. */
    __asm__ volatile ("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;                                      /* Return the byte we read */
}

/* Tiny delay: old PICs need time between commands. Port 0x80 is the legacy
 * POST-code port; writing to it takes about 1 microsecond and has no side effects. */
static inline void io_wait(void)
{
    outb(0x80, 0);
}

#endif /* IO_H */
