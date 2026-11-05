/* idt.h - Interrupt Descriptor Table and interrupt dispatch interface */
#ifndef IDT_H
#define IDT_H

#include "types.h"

/* Snapshot of the CPU state at the moment of the interrupt.
 * Field order mirrors the stack layout built by interrupt.asm (lowest address first):
 *   ds              <- pushed by isr_common
 *   edi..eax        <- pushed by PUSHAD
 *   int_no, err_code<- pushed by the stub (err_code is a dummy 0 if the CPU pushes none)
 *   eip, cs, eflags <- pushed automatically by the CPU */
typedef struct registers {
    u32 ds;
    u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;
    u32 int_no, err_code;
    u32 eip, cs, eflags;
} registers_t;

/* Signature of any function that can handle an interrupt */
typedef void (*isr_t)(registers_t *regs);

void idt_init(void);                                         /* Build and load the IDT */
void register_interrupt_handler(u8 vector, isr_t handler);   /* Attach a C handler to a vector */

#endif
