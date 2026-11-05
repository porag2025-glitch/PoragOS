/* pmm.h - Physical Memory Manager: hands out and takes back 4 KiB frames */
#ifndef PMM_H
#define PMM_H

#include "types.h"

/* Build the frame bitmap from the Multiboot memory map. Call once, before paging. */
void pmm_init(u32 multiboot_info_addr);

/* Allocate one free 4 KiB frame. Returns its PHYSICAL address, or 0 if memory is
 * exhausted. (0 is never a valid result: frame 0 is always reserved.) */
phys_addr_t pmm_alloc_frame(void);

/* Return a frame to the free pool. `frame` must be 4 KiB aligned. */
void pmm_free_frame(phys_addr_t frame);

u32 pmm_total_frames(void);     /* Frames covered by the bitmap (highest usable frame + 1) */
u32 pmm_free_frames(void);      /* Frames currently free */

#endif /* PMM_H */
