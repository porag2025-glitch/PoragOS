/* ramdisk.h - Multiboot ramdisk (initrd) block-/byte-level storage driver */
#ifndef RAMDISK_H
#define RAMDISK_H

#include "types.h"

#define RAMDISK_SECTOR_SIZE 512u

/* Locate the first Multiboot module (if any) and prepare it for reading.
 * Returns 0 if a ramdisk was found, -1 if booted without one. */
int  ramdisk_init(u32 multiboot_info_addr);

/* Returns 1 if a valid ramdisk image is present in memory, 0 otherwise. */
int  ramdisk_is_present(void);

/* Total size of the ramdisk in bytes (0 if none). */
u32  ramdisk_get_size(void);

/* Base virtual address of the ramdisk image in memory (0 if none). */
const u8 *ramdisk_get_base(void);

/* Copy `size` bytes starting at byte `offset` from the ramdisk into `dst`.
 * Returns the number of bytes actually read (clamped to the ramdisk end). */
u32  ramdisk_read(u32 offset, void *dst, u32 size);

#endif /* RAMDISK_H */
