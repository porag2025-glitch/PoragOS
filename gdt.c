/* gdt.c - flat-memory-model GDT: null, kernel code, kernel data.
 * Both segments cover the whole 4 GiB (base 0, limit 4 GiB), so segmentation is
 * effectively "turned off" and memory protection will come from paging later. */

#include "gdt.h"
#include "types.h"

/* One 8-byte segment descriptor. Packed: no compiler padding allowed. */
struct gdt_entry {
    u16 limit_low;      /* Limit bits 0-15 */
    u16 base_low;       /* Base  bits 0-15 */
    u8  base_mid;       /* Base  bits 16-23 */
    u8  access;         /* Present, privilege level, type */
    u8  granularity;    /* High nibble: flags (G, D/B). Low nibble: limit bits 16-19 */
    u8  base_high;      /* Base  bits 24-31 */
} __attribute__((packed));

/* The 6-byte operand of the LGDT instruction */
struct gdt_ptr {
    u16 limit;          /* Size of the table in bytes minus 1 */
    u32 base;           /* Linear address of the table */
} __attribute__((packed));

static struct gdt_entry gdt[3];     /* null + code + data */
static struct gdt_ptr   gdt_pointer;

extern void gdt_flush(u32 gdt_ptr_addr);   /* Defined in gdt_flush.asm */

/* Fill one descriptor */
static void gdt_set_gate(int i, u32 base, u32 limit, u8 access, u8 flags)
{
    gdt[i].base_low    = (u16)(base & 0xFFFF);          /* Scatter the base address ... */
    gdt[i].base_mid    = (u8)((base >> 16) & 0xFF);     /* ... into the three odd-shaped */
    gdt[i].base_high   = (u8)((base >> 24) & 0xFF);     /* fields (a legacy of the 286) */
    gdt[i].limit_low   = (u16)(limit & 0xFFFF);         /* Limit bits 0-15 */
    gdt[i].granularity = (u8)(((limit >> 16) & 0x0F) | (flags & 0xF0)); /* Limit 16-19 + flags */
    gdt[i].access      = access;
}

void gdt_init(void)
{
    gdt_pointer.limit = (u16)(sizeof(gdt) - 1);         /* Table size minus one */
    gdt_pointer.base  = (u32)&gdt;                      /* Where the table lives */

    gdt_set_gate(0, 0, 0, 0, 0);                        /* Entry 0: mandatory null descriptor */
    /* Entry 1: code. access 0x9A = Present | Ring 0 | Code/Data | Executable | Readable
     *          flags  0xC0 = 4 KiB granularity | 32-bit segment */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xC0);
    /* Entry 2: data. access 0x92 = Present | Ring 0 | Code/Data | Writable */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xC0);

    gdt_flush((u32)&gdt_pointer);                       /* LGDT + reload all segment registers */
}
