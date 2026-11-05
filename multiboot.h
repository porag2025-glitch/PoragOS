/* multiboot.h - the parts of the Multiboot 1 information structure we use.
 * GRUB builds this structure in memory and passes its address in EBX. */
#ifndef MULTIBOOT_H
#define MULTIBOOT_H

#include "types.h"

#define MULTIBOOT_FLAG_MEM   (1u << 0)   /* mem_lower / mem_upper fields are valid */
#define MULTIBOOT_FLAG_MODS  (1u << 3)   /* mods_count / mods_addr are valid */
#define MULTIBOOT_FLAG_MMAP  (1u << 6)   /* mmap_length / mmap_addr are valid */

#define MULTIBOOT_MEMORY_AVAILABLE 1     /* mmap entry type: ordinary usable RAM */

/* Fixed layout from the Multiboot specification. Packed: no padding allowed. */
struct multiboot_info {
    u32 flags;          /* Which of the following fields are valid (bit mask) */
    u32 mem_lower;      /* KiB of memory below 1 MiB */
    u32 mem_upper;      /* KiB of memory above 1 MiB (contiguous from 1 MiB) */
    u32 boot_device;
    u32 cmdline;
    u32 mods_count;     /* Number of boot modules loaded by GRUB */
    u32 mods_addr;      /* Physical address of the module array */
    u32 syms[4];        /* ELF/a.out symbol info (unused) */
    u32 mmap_length;    /* Total size in bytes of the memory-map buffer */
    u32 mmap_addr;      /* Physical address of the first memory-map entry */
} __attribute__((packed));

/* One memory-map entry. NOTE: `size` does NOT count itself, so the next
 * entry starts at (this entry's address + size + 4). */
struct multiboot_mmap_entry {
    u32 size;           /* Size of the rest of this entry (normally 20) */
    u64 addr;           /* Start physical address of the region */
    u64 len;            /* Length of the region in bytes */
    u32 type;           /* 1 = usable RAM, 2 = reserved, 3 = ACPI reclaimable, 4 = ACPI NVS, 5 = bad RAM */
} __attribute__((packed));

#endif /* MULTIBOOT_H */
