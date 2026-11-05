/* paging.c - 32-bit paging: identity-map low memory, then map pages on demand. */

#include "paging.h"
#include "pmm.h"
#include "idt.h"
#include "vga.h"

/* How much low memory to identity-map: 4 tables x 1024 entries x 4 KiB = 16 MiB. */
#define IDENTITY_TABLES 4u
#define IDENTITY_BYTES  (IDENTITY_TABLES * 4u * 1024u * 1024u)

/* The last directory slot (1023) points back at the directory itself. */
#define RECURSIVE_INDEX 1023u

/* Through the recursive slot the CPU shows us the paging structures at fixed addresses:
 *   page directory       : 0xFFFFF000
 *   page table number n  : 0xFFC00000 + n * 4096
 * `volatile` because the CPU itself also modifies entries (Accessed/Dirty bits). */
#define PD_VIRT        ((volatile u32 *)0xFFFFF000u)
#define PT_VIRT(n)     ((volatile u32 *)(0xFFC00000u + ((u32)(n) << 12)))

/* A page directory/table is 1024 x 4-byte entries = exactly 4 KiB and MUST be 4 KiB aligned
 * (the low 12 bits of CR3/PDE are flags, not address). Static, so they live in .bss, inside
 * the kernel image, which PMM has already reserved. */
static u32 page_directory[1024]                    __attribute__((aligned(PAGE_SIZE)));
static u32 identity_tables[IDENTITY_TABLES][1024]  __attribute__((aligned(PAGE_SIZE)));

extern u8 kernel_end[];                             /* From linker.ld */

extern void paging_load_directory(u32 phys);        /* paging_asm.asm */
extern void paging_enable(void);                    /* paging_asm.asm */

/* Invalidate the TLB entry for one virtual address. The TLB caches translations; after we
 * change a PTE the CPU may still use the stale cached copy unless we flush it. */
static inline void tlb_flush_page(u32 virt)
{
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

static void paging_panic(const char *msg)
{
    terminal_set_color(VGA_WHITE, VGA_RED);
    terminal_write("\nPAGING PANIC: ");
    terminal_write(msg);
    for (;;) __asm__ volatile ("cli; hlt");
}

/* Vector 14 handler. The CPU stores the faulting address in CR2 and pushes an error code. */
static void page_fault_handler(registers_t *regs)
{
    u32 fault_addr;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_addr));  /* CR2 = the address that caused it */
    u32 e = regs->err_code;

    terminal_set_color(VGA_WHITE, VGA_RED);
    terminal_write("\n*** PAGE FAULT ***\nAddress : ");
    terminal_write_hex(fault_addr);
    terminal_write("\nEIP     : ");
    terminal_write_hex(regs->eip);                          /* The instruction that faulted */
    terminal_write("\nCause   : ");
    terminal_write((e & 0x01) ? "protection violation" : "page not present");   /* bit 0: P */
    terminal_write((e & 0x02) ? ", write" : ", read");                          /* bit 1: W/R */
    terminal_write((e & 0x04) ? ", user mode" : ", kernel mode");               /* bit 2: U/S */
    if (e & 0x08) terminal_write(", reserved bit set");                         /* bit 3: RSVD */
    if (e & 0x10) terminal_write(", instruction fetch");                        /* bit 4: I/D */
    terminal_write("\nSystem halted.\n");

    for (;;) __asm__ volatile ("cli; hlt");     /* No demand paging yet: faults are fatal */
}

void paging_init(void)
{
    if ((u32)kernel_end > IDENTITY_BYTES) {     /* The code we are running must stay mapped */
        paging_panic("kernel larger than the identity-mapped area");
    }

    /* 1. Start with an empty directory: every entry "not present". */
    for (u32 i = 0; i < 1024; i++) {
        page_directory[i] = 0;
    }

    /* 2. Fill the identity tables: page N of RAM <-> page N of virtual space. */
    for (u32 t = 0; t < IDENTITY_TABLES; t++) {
        for (u32 i = 0; i < 1024; i++) {
            u32 addr = (t * 1024 + i) * PAGE_SIZE;              /* Physical address of this page */
            identity_tables[t][i] = (addr == 0)
                ? 0                                             /* Leave page 0 unmapped: NULL trap */
                : (addr | PAGE_PRESENT | PAGE_WRITE);
        }
        /* Hook the table into the directory. Paging is still OFF, so the address of
         * our static array already IS its physical address (we are linked at 1 MiB). */
        page_directory[t] = (u32)&identity_tables[t][0] | PAGE_PRESENT | PAGE_WRITE;
    }

    /* 3. Recursive slot: the last directory entry points at the directory itself. */
    page_directory[RECURSIVE_INDEX] = (u32)&page_directory[0] | PAGE_PRESENT | PAGE_WRITE;

    /* 4. Install the page-fault handler BEFORE turning paging on. */
    register_interrupt_handler(14, page_fault_handler);

    /* 5. Point CR3 at the directory, then set CR0.PG. Order matters: with PG on and a
     *    garbage CR3 the very next instruction fetch would triple-fault. */
    paging_load_directory((u32)&page_directory[0]);
    paging_enable();
    /* We are now running with translation on, still at the same addresses. */
}

int paging_map_page(virt_addr_t virt, phys_addr_t phys, u32 flags)
{
    u32 pdi = virt >> 22;                       /* Top 10 bits: directory index */
    u32 pti = (virt >> 12) & 0x3FF;             /* Next 10 bits: table index */

    if (pdi == RECURSIVE_INDEX) return -1;      /* Top 4 MiB is reserved for the recursive window */

    if (!(PD_VIRT[pdi] & PAGE_PRESENT)) {       /* No page table covers this region yet */
        phys_addr_t table = pmm_alloc_frame();  /* Ask the PMM for a frame to hold it */
        if (table == 0) return -1;              /* Out of memory */

        /* Install the PDE first: this makes the new table visible through the recursive
         * window, which is how we can zero it without it being identity-mapped. */
        PD_VIRT[pdi] = table | PAGE_PRESENT | PAGE_WRITE | (flags & PAGE_USER);
        tlb_flush_page((u32)PT_VIRT(pdi));

        for (u32 i = 0; i < 1024; i++) {        /* A fresh table must start all "not present" */
            PT_VIRT(pdi)[i] = 0;
        }
    }

    /* Write the PTE: frame address in the high 20 bits, flags in the low 12. */
    PT_VIRT(pdi)[pti] = (phys & 0xFFFFF000u) | (flags & 0xFFFu) | PAGE_PRESENT;
    tlb_flush_page(virt);                       /* Drop any stale cached translation */
    return 0;
}

phys_addr_t paging_unmap_page(virt_addr_t virt)
{
    u32 pdi = virt >> 22;
    u32 pti = (virt >> 12) & 0x3FF;

    if (!(PD_VIRT[pdi] & PAGE_PRESENT)) return 0;           /* No table: nothing mapped */
    u32 entry = PT_VIRT(pdi)[pti];
    if (!(entry & PAGE_PRESENT)) return 0;                  /* Page not mapped */

    PT_VIRT(pdi)[pti] = 0;                                  /* Clear the PTE ... */
    tlb_flush_page(virt);                                   /* ... and the cached translation */
    return entry & 0xFFFFF000u;                             /* Tell the caller which frame it was */
}

phys_addr_t paging_get_physical(virt_addr_t virt)
{
    u32 pdi = virt >> 22;
    u32 pti = (virt >> 12) & 0x3FF;

    if (!(PD_VIRT[pdi] & PAGE_PRESENT)) return 0;
    u32 entry = PT_VIRT(pdi)[pti];
    if (!(entry & PAGE_PRESENT)) return 0;

    return (entry & 0xFFFFF000u) | (virt & 0xFFFu);         /* frame base + offset within the page */
}
