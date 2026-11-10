/* paging.h - 32-bit two-level paging */
#ifndef PAGING_H
#define PAGING_H

#include "types.h"

/* Page-table-entry flag bits (low 12 bits of every PDE/PTE) */
#define PAGE_PRESENT 0x001      /* Entry is valid. If 0, any access raises a page fault. */
#define PAGE_WRITE   0x002      /* Writes allowed (otherwise read-only) */
#define PAGE_USER    0x004      /* Ring 3 may access (otherwise kernel only) */
#define PAGE_PWT     0x008      /* Write-through */
#define PAGE_PCD     0x010      /* Cache-disable (right for MMIO / video memory) */

/* Build the page directory, identity-map low memory, turn paging on. */
void paging_init(void);

/* Map one 4 KiB page: virtual `virt` -> physical frame `phys`.
 * Allocates a page table from the PMM if needed. Returns 0 on success, -1 on failure. */
int paging_map_page(virt_addr_t virt, phys_addr_t phys, u32 flags);

/* Map [phys, phys+bytes) at [virt, ...). All three must be 4 KiB aligned. 0 = ok, -1 = failure. */
int paging_map_range(virt_addr_t virt, phys_addr_t phys, u32 bytes, u32 flags);

/* Remove a mapping. Returns the physical frame that was mapped (0 if none).
 * It does NOT free the frame: the caller decides whether to pmm_free_frame() it. */
phys_addr_t paging_unmap_page(virt_addr_t virt);

/* Translate a virtual address by walking the tables in software.
 * Returns the physical address, or 0 if unmapped (page 0 is never mapped). */
phys_addr_t paging_get_physical(virt_addr_t virt);

#endif /* PAGING_H */
