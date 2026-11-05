/* types.h - fixed-width integer types shared by the whole kernel */
#ifndef TYPES_H
#define TYPES_H

typedef unsigned char      u8;      /* 8-bit  unsigned */
typedef unsigned short     u16;     /* 16-bit unsigned */
typedef unsigned int       u32;     /* 32-bit unsigned (int is 32 bits with gcc -m32) */
typedef unsigned long long u64;     /* 64-bit unsigned. Needed because the BIOS memory map
                                     * uses 64-bit addresses even on a 32-bit CPU.
                                     * NOTE: never DIVIDE u64 values; that needs libgcc.
                                     * Shifts, adds and compares are fine. */

/* Address types. Both are 32 bits today. When PoragOS moves to 64-bit Long Mode,
 * only these two typedefs need to change to u64. */
typedef u32 phys_addr_t;            /* An address in PHYSICAL memory (what RAM chips see) */
typedef u32 virt_addr_t;            /* An address in VIRTUAL memory (what the CPU's code uses) */

typedef u32 size_t;                 /* Byte counts. Must be `unsigned int` on -m32 so it matches
                                     * the prototypes GCC expects for memcpy/memset.[cite: 6] */

#define PAGE_SIZE 4096u             /* Size of one page / physical frame */

#define NULL ((void *)0)            /* No libc, so we define NULL ourselves */

#endif /* TYPES_H */
