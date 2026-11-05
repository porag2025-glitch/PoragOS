/* heap.h - kernel heap allocator (kmalloc / kfree) */
#ifndef HEAP_H
#define HEAP_H

#include "types.h"

/* A snapshot of the heap's state, produced by heap_get_stats(). */
struct heap_stats {
    u32 heap_size;      /* Bytes of virtual memory currently mapped for the heap */
    u32 used_bytes;     /* Payload bytes in allocated blocks */
    u32 free_bytes;     /* Payload bytes in free blocks */
    u32 used_blocks;    /* Number of allocated blocks */
    u32 free_blocks;    /* Number of free blocks (high = fragmented) */
    u32 largest_free;   /* Biggest single request that can succeed WITHOUT growing */
    u32 overhead_bytes; /* Bytes spent on block headers */
};

void  heap_init(void);                          /* Map the first pages, create one big free block.
                                                 * Requires pmm_init() AND paging_init() first. */
void *kmalloc(size_t size);                     /* 16-byte aligned block, or NULL on failure */
void *kcalloc(size_t count, size_t size);       /* Like kmalloc, zero-filled, overflow-checked */
void *krealloc(void *ptr, size_t size);         /* Resize a block (may move it) */
void  kfree(void *ptr);                         /* Release a block. kfree(NULL) is a no-op. */

void  heap_get_stats(struct heap_stats *out);   /* Fill `out` by walking the block list */
void  heap_dump(void);                          /* Print every block to the screen */
int   heap_check(void);                         /* Verify all invariants. Returns number of errors (0 = healthy) */

#endif /* HEAP_H */
