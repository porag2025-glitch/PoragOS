/* pic.h - 8259 Programmable Interrupt Controller driver */
#ifndef PIC_H
#define PIC_H

#include "types.h"

void pic_remap(u8 master_offset, u8 slave_offset);  /* Move IRQ vectors, mask all IRQs */
void pic_send_eoi(u8 irq);                          /* Acknowledge an IRQ (0-15) */
void pic_unmask_irq(u8 irq);                        /* Enable delivery of one IRQ line */
void pic_mask_irq(u8 irq);                          /* Disable delivery of one IRQ line */

#endif
