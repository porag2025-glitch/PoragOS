/* idt.c - IDT construction and the common C-level interrupt dispatcher */

#include "idt.h"
#include "gdt.h"
#include "pic.h"
#include "vga.h"

#define IDT_ENTRIES 256         /* x86 supports 256 vectors */
#define IDT_FLAGS   0x8E        /* 1000 1110b: Present | DPL 0 | 32-bit INTERRUPT gate.
                                 * An interrupt gate clears IF on entry, so we are not
                                 * interrupted while handling an interrupt. */

/* One 8-byte gate descriptor */
struct idt_entry {
    u16 base_low;               /* Handler address bits 0-15 */
    u16 selector;               /* Code segment selector to use (our GDT code entry) */
    u8  always0;                /* Reserved, must be 0 */
    u8  flags;                  /* Present bit, privilege level, gate type */
    u16 base_high;              /* Handler address bits 16-31 */
} __attribute__((packed));

/* The 6-byte operand of LIDT */
struct idt_ptr {
    u16 limit;                  /* Table size in bytes minus 1 */
    u32 base;                   /* Linear address of the table */
} __attribute__((packed));

static struct idt_entry idt[IDT_ENTRIES];       /* Static -> zero-initialised -> every gate "not present" */
static struct idt_ptr   idt_pointer;
static isr_t            handlers[IDT_ENTRIES];  /* Registered C handlers (NULL = none) */

extern void idt_flush(u32 idt_ptr_addr);        /* interrupt.asm: runs LIDT */
extern u32  isr_stub_table[48];                 /* interrupt.asm: addresses of the 48 asm stubs */

/* Human-readable names of the 32 CPU exceptions */
static const char *exception_names[32] = {
    "Divide Error", "Debug", "Non-Maskable Interrupt", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
    "x87 Floating-Point Exception", "Alignment Check", "Machine Check", "SIMD Floating-Point",
    "Virtualization", "Control Protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "VMM Communication", "Security Exception", "Reserved"
};

/* Write one gate into the IDT */
static void idt_set_gate(u8 vector, u32 handler, u16 selector, u8 flags)
{
    idt[vector].base_low  = (u16)(handler & 0xFFFF);        /* Split the 32-bit handler address */
    idt[vector].base_high = (u16)((handler >> 16) & 0xFFFF);/* into two 16-bit halves */
    idt[vector].selector  = selector;                       /* Which code segment to run it in */
    idt[vector].always0   = 0;
    idt[vector].flags     = flags;
}

void idt_init(void)
{
    idt_pointer.limit = (u16)(sizeof(idt) - 1);             /* Table size minus one */
    idt_pointer.base  = (u32)&idt;                          /* Table address */

    /* Vectors 0-31 = CPU exceptions, 32-47 = hardware IRQs 0-15 (after PIC remap) */
    for (u8 i = 0; i < 48; i++) {
        idt_set_gate(i, isr_stub_table[i], GDT_KERNEL_CODE, IDT_FLAGS);
    }

    idt_flush((u32)&idt_pointer);                           /* LIDT: the CPU now uses our table */
}

void register_interrupt_handler(u8 vector, isr_t handler)
{
    handlers[vector] = handler;                             /* Remember the handler for dispatch */
}

/* Called by isr_common in interrupt.asm for EVERY interrupt */
void isr_handler(registers_t *regs)
{
    u32 n = regs->int_no;                                   /* Which vector fired? */

    if (handlers[n] != NULL) {                              /* A handler is registered: run it */
        handlers[n](regs);
    } else if (n < 32) {                                    /* Unhandled CPU exception: fatal for now */
        terminal_set_color(VGA_WHITE, VGA_RED);
        terminal_write("\n*** CPU EXCEPTION: ");
        terminal_write(exception_names[n]);
        terminal_write(" ***\nINT=");
        terminal_write_hex(n);
        terminal_write("  ERR=");
        terminal_write_hex(regs->err_code);
        terminal_write("  EIP=");
        terminal_write_hex(regs->eip);
        terminal_write("\nSystem halted.\n");
        for (;;) {                                          /* Freeze the machine */
            __asm__ volatile ("cli; hlt");
        }
    }

    if (n >= 32 && n < 48) {                                /* It was a hardware IRQ ... */
        pic_send_eoi((u8)(n - 32));                         /* ... tell the PIC we are done */
    }
}
