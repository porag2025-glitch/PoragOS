/* gdt.h - Global Descriptor Table interface */
#ifndef GDT_H
#define GDT_H

#define GDT_KERNEL_CODE 0x08    /* Selector for GDT entry 1 (index 1 * 8 bytes) */
#define GDT_KERNEL_DATA 0x10    /* Selector for GDT entry 2 (index 2 * 8 bytes) */

void gdt_init(void);            /* Build and load our own GDT */

#endif
