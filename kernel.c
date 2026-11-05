/*
 * kernel.c - PoragOS C entry point (Phase 4: memory management)
 */

#include "types.h"
#include "vga.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "keyboard.h"
#include "shell.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002       /* Left in EAX by a Multiboot loader */

void kernel_main(u32 multiboot_magic, u32 multiboot_info_addr)
{
    terminal_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    terminal_clear();
    terminal_write("Welcome to PoragOS Kernel!\n\n");

    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    terminal_write("Multiboot magic: ");
    terminal_write_hex(multiboot_magic);
    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {    /* Without Multiboot we have no memory map */
        terminal_write("  (BAD) - cannot continue\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    terminal_write("  (OK)\n");

    gdt_init();                                     /* 1. Our own segments */
    terminal_write("[OK] GDT loaded\n");

    idt_init();                                     /* 2. Interrupt table (page-fault handler needs it) */
    terminal_write("[OK] IDT loaded\n");

    pic_remap(0x20, 0x28);                          /* 3. IRQs -> vectors 32-47 */
    terminal_write("[OK] PIC remapped\n");

    pmm_init(multiboot_info_addr);                  /* 4. Which physical frames are free? */
    terminal_write("[OK] PMM: ");
    terminal_write_dec(pmm_free_frames() >> 8);     
    terminal_write(" MiB free (");
    terminal_write_dec(pmm_free_frames());
    terminal_write(" frames)\n");

    paging_init();                                  /* 5. Build tables, load CR3, set CR0.PG */
    terminal_write("[OK] Paging enabled (identity map, first 16 MiB)\n");

    heap_init();                                    /* 6. Kernel heap on top of PMM + paging */
    terminal_write("[OK] Kernel heap ready (16 KiB at 0xE0000000)\n");

    keyboard_init();                                /* 7. Keyboard ISR + unmask IRQ1 */
    terminal_write("[OK] Keyboard driver ready\n");

    __asm__ volatile ("sti");                       /* 8. Enable interrupts LAST */

    shell_init();
    shell_run();                                    /* Never returns */
}
