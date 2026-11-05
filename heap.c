/* heap.c - kernel heap: a doubly-linked, address-ordered list of blocks.
 *
 * Memory layout inside the heap (every block is directly followed by the next one):
 *
 *   HEAP_START                                                        heap_end
 *   │                                                                    │
 *   ▼                                                                    ▼
 *   ┌────────┬───────────────┬────────┬────────────┬────────┬─────────────┐
 *   │ header │   payload     │ header │  payload   │ header │  payload    │
 *   │ (16 B) │ (USED, 112 B) │ (16 B) │ (FREE)     │ (16 B) │ (USED)      │
 *   └────────┴───────────────┴────────┴────────────┴────────┴─────────────┘
 *             ▲
 *             └── the pointer kmalloc() returns
 *
 * Invariants (heap_check() verifies them):
 *   1. Blocks tile the heap exactly: next block = this + HDR_SIZE + size.
 *   2. Every payload address and every size is a multiple of HEAP_ALIGN (16).
 *   3. No two FREE blocks are ever adjacent (kfree coalesces immediately).
 *   4. prev/next pointers agree with address order. */

#include "heap.h"
#include "pmm.h"
#include "paging.h"
#include "vga.h"
#include "kstring.h"

/* ---- Configuration ----------------------------------------------------- */
#define HEAP_START          0xE0000000u     /* First virtual address of the heap (page aligned) */
#define HEAP_LIMIT          0xF0000000u     /* The heap may never grow past here (256 MiB max) */
#define HEAP_INITIAL_PAGES  4u              /* 16 KiB mapped at boot */
#define HEAP_GROW_PAGES     4u              /* Grow by at least this many pages at a time */
#define HEAP_ALIGN          16u             /* Alignment of every payload; must be a power of two */
#define HEAP_DUMP_MAX       20u             /* heap_dump prints at most this many blocks */

#define MAGIC_FREE          0xF4EEB10Cu     /* Header tag: block is free */
#define MAGIC_USED          0xA110C8EDu     /* Header tag: block is allocated */

/* Round x UP to the next multiple of a (a must be a power of two).
 * Example a=16: 1->16, 16->16, 17->32. Adding (a-1) pushes any non-multiple past the next
 * boundary; masking with ~(a-1) clears the low bits and lands exactly on it. */
#define ALIGN_UP(x, a)      (((x) + ((a) - 1u)) & ~((a) - 1u))

/* ---- Block header ------------------------------------------------------ */
typedef struct block {
    u32            size;    /* PAYLOAD size in bytes (header not included). Multiple of 16. */
    u32            magic;   /* MAGIC_FREE or MAGIC_USED: state + corruption/double-free detector */
    struct block  *prev;    /* Block at the lower address (NULL for the first block) */
    struct block  *next;    /* Block at the higher address (NULL for the last block) */
} block_t;

#define HDR_SIZE     ((u32)sizeof(block_t))     /* 16 on 32-bit x86 */
#define MIN_PAYLOAD  HEAP_ALIGN                 /* Never create a block with less than 16 bytes of payload */

/* Compile-time check: if this fails, the header size broke payload alignment.
 * (A negative array size is a compile error. Useful when porting to 64-bit: pointers grow.) */
typedef char heap_header_alignment_check[(sizeof(block_t) % HEAP_ALIGN == 0) ? 1 : -1];

/* ---- Heap state -------------------------------------------------------- */
static block_t *heap_head  = NULL;      /* Lowest-address block */
static block_t *heap_tail  = NULL;      /* Highest-address block (the one next to heap_end) */
static u32      heap_end   = HEAP_START;/* First UNMAPPED heap address; grows upwards */
static int      heap_ready = 0;         /* Set once heap_init succeeded */

/* ---- Interrupt-safety helpers ------------------------------------------ */
/* Disable interrupts and return the previous EFLAGS, so nested use works:
 * an inner restore re-enables interrupts only if they were on to begin with. */
static inline u32 irq_save(void)
{
    u32 flags;
    __asm__ volatile ("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(u32 flags)
{
    if (flags & 0x200u) {                               /* EFLAGS bit 9 = IF (interrupts were enabled) */
        __asm__ volatile ("sti" : : : "memory");
    }
}

/* ---- Small helpers ----------------------------------------------------- */
static inline void *block_payload(block_t *b)
{
    return (void *)((u8 *)b + HDR_SIZE);                /* The payload starts right after the header */
}

/* Fatal error: the heap's own metadata is damaged, so continuing could corrupt more. */
static void heap_panic(const char *msg, void *where)
{
    terminal_set_color(VGA_WHITE, VGA_RED);
    terminal_write("\nHEAP PANIC: ");
    terminal_write(msg);
    terminal_write(" at ");
    terminal_write_hex((u32)where);
    terminal_write("\nSystem halted.\n");
    for (;;) __asm__ volatile ("cli; hlt");
}

/* Non-fatal complaint (bad pointer passed by a caller). */
static void heap_warn(const char *msg, void *where)
{
    terminal_set_color(VGA_LIGHT_RED, VGA_BLACK);
    terminal_write("[heap] ");
    terminal_write(msg);
    terminal_write(": ");
    terminal_write_hex((u32)where);
    terminal_putchar('\n');
    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
}

/* Validate a pointer given to kfree/krealloc and return its header,
 * or NULL (after printing why) if it is not a live allocation. */
static block_t *block_from_ptr(void *ptr)
{
    u32 a = (u32)ptr;

    if (a < HEAP_START + HDR_SIZE || a >= heap_end) {   /* Outside the mapped heap */
        heap_warn("pointer outside heap", ptr);
        return NULL;
    }
    if (a & (HEAP_ALIGN - 1u)) {                        /* Not 16-aligned: cannot be a payload start */
        heap_warn("misaligned pointer", ptr);
        return NULL;
    }
    block_t *b = (block_t *)(a - HDR_SIZE);             /* Step back over the header */
    if (b->magic == MAGIC_FREE) {                       /* Already freed */
        heap_warn("double free detected", ptr);
        return NULL;
    }
    if (b->magic != MAGIC_USED) {                       /* Neither tag: not from kmalloc, or header trampled */
        heap_warn("invalid pointer (bad header magic)", ptr);
        return NULL;
    }
    return b;
}

/* ---- Splitting and coalescing (the core of the algorithm) --------------- */

/* Absorb `b->next` into `b` if that neighbour is free.
 *
 *   before:  [b: hdr|payload] [n: hdr|payload (FREE)] [x ...]
 *   after:   [b: hdr|       payload + n.hdr + n.payload       ] [x ...]
 *
 * n's header becomes part of b's payload, so the 16 header bytes are recovered. */
static void merge_with_next(block_t *b)
{
    block_t *n = b->next;
    if (n == NULL || n->magic != MAGIC_FREE) {          /* Nothing to merge */
        return;
    }
    b->size += HDR_SIZE + n->size;                      /* Grow b over n's header and payload */
    b->next  = n->next;                                 /* Unlink n from the list ... */
    if (n->next != NULL) {
        n->next->prev = b;
    } else {
        heap_tail = b;                                  /* n was the tail; b is the tail now */
    }
    n->magic = 0;                                       /* Poison the dead header: stale pointers get caught */
}

/* Cut block `b` so its payload is exactly `size` bytes, if the leftover is big enough
 * to become a block of its own.
 *
 *   before:  [b: hdr|............ 208 bytes ............]
 *   after:   [b: hdr|.. 160 ..][rest: hdr|.. 32 bytes ..]     (208 - 160 - 16 = 32)
 *
 * If the leftover is smaller than HDR_SIZE + MIN_PAYLOAD it cannot hold a header plus
 * a useful payload, so we keep it inside b (this waste is "internal fragmentation"). */
static void split_block(block_t *b, u32 size)
{
    if (b->size < size + HDR_SIZE + MIN_PAYLOAD) {      /* Leftover too small to be a block */
        return;
    }
    block_t *rest = (block_t *)((u8 *)b + HDR_SIZE + size);  /* New header goes right after b's shrunk payload */
    rest->size  = b->size - size - HDR_SIZE;            /* Remaining bytes minus the new header itself */
    rest->magic = MAGIC_FREE;
    rest->prev  = b;                                    /* Insert `rest` between b and b->next */
    rest->next  = b->next;
    if (b->next != NULL) {
        b->next->prev = rest;
    } else {
        heap_tail = rest;
    }
    b->next = rest;
    b->size = size;

    merge_with_next(rest);                              /* If a free block follows, fuse them (keeps invariant 3) */
}

/* ---- Growing the heap -------------------------------------------------- */

/* Undo the mapping of `count` pages starting at `start` and return their frames to the PMM. */
static void heap_rollback(u32 start, u32 count)
{
    while (count > 0) {
        count--;
        phys_addr_t frame = paging_unmap_page(start + count * PAGE_SIZE);
        if (frame != 0) {
            pmm_free_frame(frame);
        }
    }
}

/* Map `pages` new pages at heap_end and fold them into the block list. All-or-nothing:
 * on failure every page mapped by this call is released again. Returns 0 or -1. */
static int heap_extend(u32 pages)
{
    u32 avail = (HEAP_LIMIT - heap_end) / PAGE_SIZE;    /* Pages left before the heap's virtual limit */
    if (pages == 0 || pages > avail) {
        return -1;
    }

    u32 start = heap_end;
    for (u32 i = 0; i < pages; i++) {
        phys_addr_t frame = pmm_alloc_frame();          /* 1. Get a physical frame from the PMM ... */
        if (frame == 0) {                               /*    Out of physical memory */
            heap_rollback(start, i);
            return -1;
        }
        /* 2. ... and map it at the next heap address. paging_map_page may itself allocate
         *    a page-table frame (once per 4 MiB of heap). */
        if (paging_map_page(start + i * PAGE_SIZE, frame, PAGE_WRITE) != 0) {
            pmm_free_frame(frame);
            heap_rollback(start, i);
            return -1;
        }
    }

    u32 bytes = pages * PAGE_SIZE;                      /* The newly usable bytes, contiguous with the old heap */

    if (heap_tail == NULL) {                            /* First ever call: create the first block */
        block_t *b = (block_t *)start;
        b->size  = bytes - HDR_SIZE;
        b->magic = MAGIC_FREE;
        b->prev  = NULL;
        b->next  = NULL;
        heap_head = b;
        heap_tail = b;
    } else if (heap_tail->magic == MAGIC_FREE) {        /* Last block is free: simply make it bigger */
        heap_tail->size += bytes;
    } else {                                            /* Last block is in use: append a new free block */
        block_t *b = (block_t *)start;
        b->size  = bytes - HDR_SIZE;
        b->magic = MAGIC_FREE;
        b->prev  = heap_tail;
        b->next  = NULL;
        heap_tail->next = b;
        heap_tail = b;
    }

    heap_end += bytes;
    return 0;
}

/* Make the heap grow so a request of `payload` bytes can succeed. Called when first-fit
 * found nothing. Returns 0 on success, -1 if memory or address space ran out. */
static int heap_grow(u32 payload)
{
    /* If the tail is free, the new pages merge into it, so we only need the shortfall.
     * Otherwise we need room for a brand-new header plus the payload. */
    u32 need = (heap_tail->magic == MAGIC_FREE) ? payload - heap_tail->size
                                                : payload + HDR_SIZE;
    u32 pages = ALIGN_UP(need, PAGE_SIZE) / PAGE_SIZE;
    u32 avail = (HEAP_LIMIT - heap_end) / PAGE_SIZE;
    if (pages > avail) {
        return -1;                                      /* Not even the minimum fits in the address range */
    }

    u32 extra = (pages < HEAP_GROW_PAGES) ? HEAP_GROW_PAGES : pages;  /* Grow in chunks, not page by page */
    if (extra > avail) extra = avail;

    if (heap_extend(extra) == 0) return 0;
    if (extra > pages) return heap_extend(pages);       /* Memory is tight: retry with the bare minimum */
    return -1;
}

/* ---- Public API -------------------------------------------------------- */

void heap_init(void)
{
    heap_head = NULL;
    heap_tail = NULL;
    heap_end  = HEAP_START;
    if (heap_extend(HEAP_INITIAL_PAGES) != 0) {         /* Needs working PMM and paging */
        heap_panic("cannot map the initial heap", (void *)HEAP_START);
    }
    heap_ready = 1;
}

/* First fit: return the first free block (lowest address) that is big enough. */
static block_t *find_fit(u32 size)
{
    for (block_t *b = heap_head; b != NULL; b = b->next) {
        if (b->magic == MAGIC_FREE) {
            if (b->size >= size) return b;
        } else if (b->magic != MAGIC_USED) {            /* A header that is neither: memory was overwritten */
            heap_panic("corrupted block header", b);
        }
    }
    return NULL;
}

void *kmalloc(size_t size)
{
    if (!heap_ready || size == 0) return NULL;
    if (size > HEAP_LIMIT - HEAP_START) return NULL;    /* Impossible request (also stops ALIGN_UP overflow) */

    size = ALIGN_UP(size, HEAP_ALIGN);                  /* Every payload size is a multiple of 16 */

    u32 flags = irq_save();                             /* ---- critical section ---- */

    block_t *b = find_fit(size);
    if (b == NULL) {                                    /* No hole big enough: ask the PMM for more memory */
        if (heap_grow(size) == 0) {
            b = find_fit(size);                         /* Will now succeed (the tail is big enough) */
        }
    }
    if (b != NULL) {
        split_block(b, size);                           /* Give back whatever we do not need */
        b->magic = MAGIC_USED;                          /* Claim it */
    }

    irq_restore(flags);                                 /* ---- end critical section ---- */
    return (b != NULL) ? block_payload(b) : NULL;
}

void kfree(void *ptr)
{
    if (ptr == NULL) return;                            /* Like free(NULL): allowed, does nothing */

    u32 flags = irq_save();

    block_t *b = block_from_ptr(ptr);                   /* Validates and prints a warning on misuse */
    if (b != NULL) {
        b->magic = MAGIC_FREE;                          /* Mark free ... */
        merge_with_next(b);                             /* ... fuse with a free block above ... */
        if (b->prev != NULL && b->prev->magic == MAGIC_FREE) {
            merge_with_next(b->prev);                   /* ... and with a free block below (b disappears into prev) */
        }
    }

    irq_restore(flags);
}

void *kcalloc(size_t count, size_t size)
{
    if (count != 0 && size > 0xFFFFFFFFu / count) {     /* count*size would overflow 32 bits: refuse */
        return NULL;
    }
    size_t total = count * size;
    void *p = kmalloc(total);
    if (p != NULL) {
        memset(p, 0, total);                            /* Recycled memory holds old data: zero it */
    }
    return p;
}

void *krealloc(void *ptr, size_t size)
{
    if (ptr == NULL) return kmalloc(size);              /* realloc(NULL, n) == malloc(n) */
    if (size == 0) { kfree(ptr); return NULL; }         /* realloc(p, 0) == free(p) */
    if (size > HEAP_LIMIT - HEAP_START) return NULL;

    u32 need  = ALIGN_UP(size, HEAP_ALIGN);
    u32 flags = irq_save();

    block_t *b = block_from_ptr(ptr);
    if (b == NULL) { irq_restore(flags); return NULL; }

    /* Cheap path 1: a free neighbour above gives us enough room to grow in place. */
    if (b->size < need && b->next != NULL && b->next->magic == MAGIC_FREE &&
        b->size + HDR_SIZE + b->next->size >= need) {
        merge_with_next(b);
    }
    /* Cheap path 2: already big enough (original, or just enlarged). Trim any big excess. */
    if (b->size >= need) {
        split_block(b, need);
        irq_restore(flags);
        return ptr;                                     /* Same address: nothing to copy */
    }

    u32 old_size = b->size;                             /* Slow path: allocate, copy, free */
    irq_restore(flags);

    void *fresh = kmalloc(size);
    if (fresh == NULL) return NULL;                     /* The old block stays valid, as in standard realloc */
    memcpy(fresh, ptr, old_size);                       /* old_size < size here, so this cannot overflow */
    kfree(ptr);
    return fresh;
}

/* ---- Diagnostics ------------------------------------------------------- */

void heap_get_stats(struct heap_stats *out)
{
    u32 flags = irq_save();

    out->heap_size = heap_end - HEAP_START;
    out->used_bytes = out->free_bytes = 0;
    out->used_blocks = out->free_blocks = 0;
    out->largest_free = 0;

    for (block_t *b = heap_head; b != NULL; b = b->next) {
        if (b->magic == MAGIC_USED) {
            out->used_blocks++;
            out->used_bytes += b->size;
        } else {
            out->free_blocks++;
            out->free_bytes += b->size;
            if (b->size > out->largest_free) out->largest_free = b->size;
        }
    }
    out->overhead_bytes = (out->used_blocks + out->free_blocks) * HDR_SIZE;

    irq_restore(flags);
}

void heap_dump(void)
{
    u32 flags = irq_save();

    terminal_write("Heap ");
    terminal_write_hex(HEAP_START);
    terminal_write(" - ");
    terminal_write_hex(heap_end);
    terminal_write(" (");
    terminal_write_dec((heap_end - HEAP_START) >> 10);
    terminal_write(" KiB)\n");

    u32 n = 0;
    for (block_t *b = heap_head; b != NULL; b = b->next) {
        if (n++ == HEAP_DUMP_MAX) {                     /* Do not flood the 25-line screen */
            terminal_write("  ... (more blocks)\n");
            break;
        }
        terminal_write("  hdr ");
        terminal_write_hex((u32)b);
        terminal_write("  data ");
        terminal_write_hex((u32)block_payload(b));
        terminal_write("  size ");
        terminal_write_dec(b->size);
        terminal_write(b->magic == MAGIC_FREE ? "  FREE\n" : "  USED\n");
    }

    irq_restore(flags);
}

int heap_check(void)
{
    u32 flags = irq_save();
    int errors = 0;
    u32 expected = HEAP_START;                          /* Where the next header MUST be (invariant 1) */
    block_t *prev = NULL;

    for (block_t *b = heap_head; b != NULL; b = b->next) {
        if ((u32)b != expected) {                       /* Gap or overlap between blocks */
            heap_warn("block not contiguous", b); errors++; break;
        }
        if (b->magic != MAGIC_FREE && b->magic != MAGIC_USED) {
            heap_warn("bad magic", b); errors++; break; /* Cannot trust b->next any more */
        }
        if (b->size % HEAP_ALIGN != 0 || b->size > heap_end - HEAP_START) {
            heap_warn("bad block size", b); errors++; break;
        }
        if (b->prev != prev) {
            heap_warn("broken prev link", b); errors++;
        }
        if (prev != NULL && prev->magic == MAGIC_FREE && b->magic == MAGIC_FREE) {
            heap_warn("uncoalesced free blocks", b); errors++;
        }
        expected = (u32)b + HDR_SIZE + b->size;         /* Address of the following header */
        prev = b;
    }
    if (errors == 0 && (expected != heap_end || prev != heap_tail)) {
        heap_warn("list does not end at heap_end", (void *)expected);
        errors++;
    }

    irq_restore(flags);
    return errors;
}
