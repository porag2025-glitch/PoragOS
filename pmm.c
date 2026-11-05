/* pmm.c - bitmap-based physical frame allocator (4 KiB frames, up to 4 GiB of RAM).
 *
 * The bitmap itself is a static array in .bss: 1,048,576 frames / 8 = 128 KiB.
 * That costs nothing on disk (.bss is zero-filled at load time) and it avoids
 * the chicken-and-egg problem of needing an allocator to build the allocator. */

#include "pmm.h"
#include "multiboot.h"
#include "vga.h"

#define PMM_MAX_FRAMES  1048576u                    /* 4 GiB / 4 KiB */
#define BITMAP_WORDS    (PMM_MAX_FRAMES / 32u)      /* 32 frames per u32 word */
#define ADDR_LIMIT      (1ULL << 32)                /* We only track the first 4 GiB */

/* Symbols created by linker.ld: their ADDRESS is the value we want. */
extern u8 kernel_start[];
extern u8 kernel_end[];

static u32 frame_bitmap[BITMAP_WORDS];              /* 1 = used/reserved, 0 = free */
static u32 highest_frame = 0;                       /* One past the last frame we track */
static u32 free_frame_count = 0;                    /* How many bits are currently 0 */
static u32 search_word = 0;                         /* Allocation hint: no free frame before this word */

/* ---- Tiny bit helpers -------------------------------------------------- */

static inline int frame_is_used(u32 f)
{
    return (int)((frame_bitmap[f >> 5] >> (f & 31)) & 1u);  /* word = f/32, bit = f%32 */
}

static void frame_mark_used(u32 f)
{
    if (!frame_is_used(f)) {                        /* Only count a real free -> used transition */
        frame_bitmap[f >> 5] |= (1u << (f & 31));   /* Set the bit */
        free_frame_count--;
    }
}

static void frame_mark_free(u32 f)
{
    if (frame_is_used(f)) {                         /* Only count a real used -> free transition */
        frame_bitmap[f >> 5] &= ~(1u << (f & 31));  /* Clear the bit */
        free_frame_count++;
        if ((f >> 5) < search_word) {               /* Keep the allocation hint valid */
            search_word = f >> 5;
        }
    }
}

/* ---- Region helpers ---------------------------------------------------- */

/* Free every frame that lies ENTIRELY inside [base, base+len).
 * Partial frames at the edges are skipped (base rounds up, end rounds down). */
static void add_usable_region(u64 base, u64 len)
{
    u64 end = base + len;
    if (base >= ADDR_LIMIT) return;                 /* Entirely above 4 GiB: ignore */
    if (end > ADDR_LIMIT) end = ADDR_LIMIT;         /* Clamp the part we can address */

    u32 first = (u32)((base + PAGE_SIZE - 1) >> 12);/* Round the start UP to a frame boundary */
    u32 last  = (u32)(end >> 12);                   /* Round the end DOWN (exclusive) */

    for (u32 f = first; f < last; f++) {
        frame_mark_free(f);
    }
    if (last > highest_frame) {                     /* Remember how far the bitmap is meaningful */
        highest_frame = last;
    }
}

/* Mark every frame TOUCHED by the byte range [start, end) as used.
 * Opposite rounding to above: reserving must be generous. */
static void reserve_range(u32 start, u32 end)
{
    u32 first = start >> 12;                                    /* Round the start DOWN */
    u32 last  = (u32)(((u64)end + PAGE_SIZE - 1) >> 12);        /* Round the end UP (u64: no overflow) */
    if (last > PMM_MAX_FRAMES) last = PMM_MAX_FRAMES;
    for (u32 f = first; f < last; f++) {
        frame_mark_used(f);
    }
}

/* Print one memory-map line during boot (educational) */
static void print_region(u64 base, u64 len, u32 type)
{
    terminal_write("  ");
    if ((base >> 32) == 0) {
        terminal_write_hex((u32)base);
    } else {
        terminal_write("  >4GiB   ");
    }
    terminal_write("  +");
    terminal_write_dec((u32)(len >> 10));           /* Length in KiB */
    terminal_write(" KiB  ");
    switch (type) {
        case 1:  terminal_write("usable\n");        break;
        case 2:  terminal_write("reserved\n");      break;
        case 3:  terminal_write("ACPI reclaim\n");  break;
        case 4:  terminal_write("ACPI NVS\n");      break;
        default: terminal_write("other\n");         break;
    }
}

/* ---- Public API -------------------------------------------------------- */

void pmm_init(u32 mbi_addr)
{
    const struct multiboot_info *mbi = (const struct multiboot_info *)mbi_addr;

    /* Step 1: everything starts as USED (all bits 1). */
    for (u32 w = 0; w < BITMAP_WORDS; w++) {
        frame_bitmap[w] = 0xFFFFFFFFu;
    }
    free_frame_count = 0;
    highest_frame    = 0;
    search_word      = 0;

    /* Step 2: free the regions the firmware says are usable RAM. */
    if (mbi->flags & MULTIBOOT_FLAG_MMAP) {
        terminal_write("[PMM] BIOS memory map:\n");
        u32 p   = mbi->mmap_addr;                   /* Walk the entries ... */
        u32 end = p + mbi->mmap_length;             /* ... until the buffer ends */
        while (p + sizeof(struct multiboot_mmap_entry) <= end) {
            const struct multiboot_mmap_entry *e = (const struct multiboot_mmap_entry *)p;
            print_region(e->addr, e->len, e->type);
            if (e->type == MULTIBOOT_MEMORY_AVAILABLE) {
                add_usable_region(e->addr, e->len);
            }
            p += e->size + 4;                       /* `size` excludes itself, hence +4 */
        }
    } else if (mbi->flags & MULTIBOOT_FLAG_MEM) {
        /* Fallback: the loader only gave "KiB above 1 MiB". */
        terminal_write("[PMM] No memory map, using mem_upper fallback\n");
        add_usable_region(0x100000ULL, (u64)mbi->mem_upper * 1024ULL);
    } else {
        terminal_write("[PMM] FATAL: bootloader gave no memory information\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }

    /* Step 3: re-reserve everything that must never be handed out. */
    reserve_range(0, 0x100000);                                 /* First 1 MiB: BIOS data, VGA memory, ROMs */
    reserve_range((u32)kernel_start, (u32)kernel_end);          /* Our own code, data, bss, stack, bitmap */
    reserve_range(mbi_addr, mbi_addr + (u32)sizeof(*mbi));      /* The Multiboot info structure */
    if (mbi->flags & MULTIBOOT_FLAG_MMAP) {
        reserve_range(mbi->mmap_addr, mbi->mmap_addr + mbi->mmap_length);   /* The memory-map buffer */
    }
    if (mbi->flags & MULTIBOOT_FLAG_MODS) {                     /* Boot modules (none today, but be safe) */
        const u32 *mod = (const u32 *)mbi->mods_addr;           /* Each entry: start, end, string, reserved */
        reserve_range(mbi->mods_addr, mbi->mods_addr + mbi->mods_count * 16);
        for (u32 i = 0; i < mbi->mods_count; i++) {
            reserve_range(mod[i * 4], mod[i * 4 + 1]);
        }
    }
}

phys_addr_t pmm_alloc_frame(void)
{
    u32 words = (highest_frame + 31) / 32;          /* Only scan the meaningful part of the bitmap */

    for (u32 w = search_word; w < words; w++) {
        if (frame_bitmap[w] == 0xFFFFFFFFu) {       /* All 32 frames used: skip the word quickly */
            continue;
        }
        for (u32 b = 0; b < 32; b++) {              /* Find the first zero bit inside the word */
            if (!(frame_bitmap[w] & (1u << b))) {
                frame_bitmap[w] |= (1u << b);       /* Claim it */
                free_frame_count--;
                search_word = w;                    /* Nothing free before this word */
                return (phys_addr_t)((w * 32 + b) * PAGE_SIZE);   /* frame number -> address */
            }
        }
    }
    return 0;                                       /* Out of memory */
}

void pmm_free_frame(phys_addr_t frame)
{
    if (frame & (PAGE_SIZE - 1)) return;            /* Not 4 KiB aligned: refuse */
    u32 f = frame >> 12;                            /* Address -> frame number */
    if (f >= highest_frame) return;                 /* Outside the memory we manage */
    frame_mark_free(f);                              /* Double frees are harmless (bit already 0) */
}

u32 pmm_total_frames(void) { return highest_frame; }
u32 pmm_free_frames(void)  { return free_frame_count; }
