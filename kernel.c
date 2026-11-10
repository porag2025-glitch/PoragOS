/*
 * kernel.c - PoragOS C entry point
 * (PMM, paging, framebuffer, heap, filesystem, PIT, multitasking, keyboard, mouse,
 *  mouse cursor, shell)
 */

#include "types.h"
#include "serial.h"
#include "vga.h"
#include "gdt.h"
#include "idt.h"
#include "pic.h"
#include "pmm.h"
#include "paging.h"
#include "multiboot.h"
#include "fb.h"
#include "fbcon.h"
#include "heap.h"
#include "ramdisk.h"
#include "fs.h"
#include "pit.h"
#include "task.h"
#include "keyboard.h"
#include "mouse.h"
#include "cursor.h"
#include "shell.h"

#define MULTIBOOT_BOOTLOADER_MAGIC 0x2BADB002

/* Shared counter updated by the `heartbeat` task */
static volatile u32 heartbeat_steps = 0;

/* Background task 1: live uptime + spinner badge in the top-right corner of row 0 */
static void hud_task_entry(void *arg)
{
    (void)arg;
    const char spinner[4] = { '|', '/', '-', '\\' };
    const u8   badge_color = (u8)(VGA_BLACK | (VGA_LIGHT_CYAN << 4));

    for (;;) {
        u32 secs = pit_get_ticks() / pit_get_frequency();
        char buf[22] = "[Up:0000s | Work:000]";

        buf[4]  = (char)('0' + ((secs / 1000u) % 10u));
        buf[5]  = (char)('0' + ((secs / 100u)  % 10u));
        buf[6]  = (char)('0' + ((secs / 10u)   % 10u));
        buf[7]  = (char)('0' + (secs % 10u));

        u32 w = heartbeat_steps;
        buf[17] = (char)('0' + ((w / 100u) % 10u));
        buf[18] = (char)('0' + ((w / 10u)  % 10u));
        buf[19] = spinner[w & 3u];

        for (u32 i = 0; i < 21; i++) {
            vga_put_at(0, 80 - 21 + i, buf[i], badge_color);
        }
        task_sleep(200);
    }
}

/* Background task 2: periodic work */
static void heartbeat_task_entry(void *arg)
{
    (void)arg;
    for (;;) {
        heartbeat_steps++;
        task_sleep(250);
    }
}

/* Background task 3: keeps the on-screen mouse cursor in sync with the mouse driver.
 * sleep(10) = wake on the next 100 Hz tick; cursor_draw() returns at once if nothing moved. */
static void cursor_task_entry(void *arg)
{
    (void)arg;
    for (;;) {
        cursor_draw();
        task_sleep(10);
    }
}

void kernel_main(u32 multiboot_magic, u32 multiboot_info_addr)
{
    serial_init();                                  /* FIRST: every later failure becomes visible on COM1 */
    serial_write("PoragOS: kernel_main reached\n");

    terminal_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    terminal_clear();
    terminal_write("Welcome to PoragOS Kernel!\n\n");

    terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    terminal_write("Multiboot magic: ");
    terminal_write_hex(multiboot_magic);
    if (multiboot_magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        terminal_write("  (BAD) - cannot continue\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    terminal_write("  (OK)\n");

    gdt_init();
    terminal_write("[OK] GDT loaded\n");

    idt_init();
    terminal_write("[OK] IDT loaded\n");

    pic_remap(0x20, 0x28);
    terminal_write("[OK] PIC remapped\n");

    pmm_init(multiboot_info_addr);
    terminal_write("[OK] PMM: ");
    terminal_write_dec(pmm_free_frames() >> 8);
    terminal_write(" MiB free (");
    terminal_write_dec(pmm_free_frames());
    terminal_write(" frames)\n");

    paging_init();
    terminal_write("[OK] Paging enabled (identity map, first 16 MiB)\n");

    /* Framebuffer: needs paging, not the heap. Falls back to text mode on any problem. */
    const struct multiboot_info *mbi = (const struct multiboot_info *)multiboot_info_addr;
    int gfx = (fb_init(mbi) == 0);
    if (gfx) fb_splash();

    heap_init();
    terminal_write("[OK] Kernel heap ready (16 KiB at 0xE0000000)\n");

    fs_init(multiboot_info_addr);
    terminal_write("[OK] PoragFS mounted (");
    terminal_write_dec(fs_count());
    terminal_write(" files, initrd: ");
    if (ramdisk_is_present()) {
        terminal_write_dec(ramdisk_get_size());
        terminal_write(" bytes)\n");
    } else {
        terminal_write("built-in fallback)\n");
    }

    pit_init(PIT_DEFAULT_HZ);
    terminal_write("[OK] PIT timer running at 100 Hz\n");

    tasking_init();
    task_create("hud",       hud_task_entry,       NULL);
    task_create("heartbeat", heartbeat_task_entry, NULL);
    terminal_write("[OK] Multitasking active (PID 0 shell + 2 background tasks)\n");

    keyboard_init();
    terminal_write("[OK] Keyboard driver ready\n");

    /* Mouse: must run BEFORE sti, because init polls the 8042 and must not race with IRQ12.
     * Confine the pointer to the framebuffer if we have one, otherwise to the 80x25 text grid. */
    if (gfx) {
        mouse_set_bounds(fb_get()->width, fb_get()->height);
    } else {
        mouse_set_bounds(80, 25);
    }
    mouse_init();
    terminal_write(mouse_is_ready() ? "[OK] PS/2 mouse ready (IRQ12)\n"
                                    : "[--] PS/2 mouse not available\n");

    __asm__ volatile ("sti");                       /* Interrupts on: scheduler, keyboard, mouse go live */

    if (gfx && fbcon_init() == 0) {
        task_sleep(1500);                           /* Keep the splash visible for 1.5 s */
        fbcon_enable();                             /* terminal_* now draws with pixels */
        terminal_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        terminal_clear();
    }

    /* Mouse cursor: only meaningful with a framebuffer AND a working mouse.
     * Started after the console is on screen, so the splash is never captured as "background". */
    if (gfx && mouse_is_ready()) {
        cursor_init();
        task_create("cursor", cursor_task_entry, NULL);
        terminal_write("[OK] Mouse cursor active\n");
    }

    shell_init();
    shell_run();                                    /* Runs as PID 0; never returns */
}
