/* fb.c - linear framebuffer setup from Multiboot info, plus basic drawing. */
#include "fb.h"
#include "paging.h"
#include "serial.h"

#define FB_VIRT_BASE  0xD8000000u       /* Our chosen window for the LFB */
#define FB_VIRT_LIMIT 0xE0000000u       /* Heap starts here: never cross it */

static struct framebuffer fb;

const struct framebuffer *fb_get(void) { return &fb; }

int fb_init(const struct multiboot_info *mbi)
{
    fb.ready = 0;

    /* 1. Did the loader describe a framebuffer at all? */
    if (!(mbi->flags & MULTIBOOT_FLAG_FRAMEBUFFER)) {
        serial_write("[fb] no framebuffer info (header video request ignored?)\n");
        return -1;
    }

    serial_write("[fb] addr=");  serial_write_hex((u32)mbi->framebuffer_addr);
    serial_write(" type=");      serial_write_hex(mbi->framebuffer_type);
    serial_write(" bpp=");       serial_write_hex(mbi->framebuffer_bpp);
    serial_write(" pitch=");     serial_write_hex(mbi->framebuffer_pitch);
    serial_write(" w=");         serial_write_hex(mbi->framebuffer_width);
    serial_write(" h=");         serial_write_hex(mbi->framebuffer_height);
    serial_write("\n");

    /* 2. Bit 12 is set for TEXT mode too (type 2, addr 0xB8000). Accept only direct-colour 32bpp. */
    if (mbi->framebuffer_type != 1) { serial_write("[fb] not a direct-RGB mode (text fallback)\n"); return -1; }
    if (mbi->framebuffer_bpp  != 32) { serial_write("[fb] only 32 bpp supported\n"); return -1; }
    if (mbi->framebuffer_addr >> 32) { serial_write("[fb] LFB above 4 GiB\n"); return -1; }

    u32 phys  = (u32)mbi->framebuffer_addr;
    u32 w     = mbi->framebuffer_width;
    u32 h     = mbi->framebuffer_height;
    u32 pitch = mbi->framebuffer_pitch;
    if (w == 0 || h == 0 || pitch < w * 4) { serial_write("[fb] bad geometry\n"); return -1; }

    /* 3. Map exactly the pages the buffer touches, at our fixed virtual window. */
    u32 page_off  = phys & (PAGE_SIZE - 1);
    u32 map_bytes = (pitch * h + page_off + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (map_bytes > FB_VIRT_LIMIT - FB_VIRT_BASE) { serial_write("[fb] too large\n"); return -1; }

    if (paging_map_range(FB_VIRT_BASE, phys & ~(PAGE_SIZE - 1), map_bytes,
                         PAGE_WRITE | PAGE_PWT | PAGE_PCD) != 0) {
        serial_write("[fb] paging_map_range failed\n");
        return -1;
    }

    fb.base = (u8 *)(FB_VIRT_BASE + page_off);
    fb.width = w; fb.height = h; fb.pitch = pitch; fb.bpp = 32;
    fb.red_pos = mbi->red_pos;     fb.red_size = mbi->red_size;
    fb.green_pos = mbi->green_pos; fb.green_size = mbi->green_size;
    fb.blue_pos = mbi->blue_pos;   fb.blue_size = mbi->blue_size;
    fb.ready = 1;
    serial_write("[fb] mapped OK\n");
    return 0;
}

/* Pack 8-bit channels using the positions/sizes GRUB reported (usually 16/8/0 -> 0x00RRGGBB). */
u32 fb_rgb(u8 r, u8 g, u8 b)
{
    return ((u32)(r >> (8 - fb.red_size))   << fb.red_pos)   |
           ((u32)(g >> (8 - fb.green_size)) << fb.green_pos) |
           ((u32)(b >> (8 - fb.blue_size))  << fb.blue_pos);
}

void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color)
{
    if (!fb.ready || x >= fb.width || y >= fb.height) return;
    if (x + w > fb.width)  w = fb.width  - x;                 /* Clip to the screen */
    if (y + h > fb.height) h = fb.height - y;
    for (u32 row = 0; row < h; row++) {
        volatile u32 *p = (volatile u32 *)(fb.base + (y + row) * fb.pitch) + x;  /* pitch, NOT width*4 */
        for (u32 col = 0; col < w; col++) p[col] = color;
    }
}

/* Splash: vertical gradient, a blocky "P" logo, and a progress-bar frame. No font needed. */
void fb_splash(void)
{
    if (!fb.ready) return;

    for (u32 y = 0; y < fb.height; y++) {                      /* Dark-blue -> near-black gradient */
        u8 shade = (u8)(60 - (y * 50) / fb.height);
        fb_fill_rect(0, y, fb.width, 1, fb_rgb(0, (u8)(shade / 2), shade));
    }

    u32 cx = fb.width / 2, cy = fb.height / 2 - 60;
    u32 white = fb_rgb(255, 255, 255), cyan = fb_rgb(0, 200, 255);
    fb_fill_rect(cx - 70, cy - 90, 40, 180, white);            /* Stem of the P */
    fb_fill_rect(cx - 70, cy - 90, 130, 40, white);            /* Top bar */
    fb_fill_rect(cx - 70, cy - 10, 130, 40, white);            /* Middle bar */
    fb_fill_rect(cx + 20, cy - 90, 40, 120, white);            /* Bowl's right side */
    fb_fill_rect(cx - 70, cy + 100, 140, 6, cyan);             /* Underline accent */

    fb_fill_rect(cx - 200, cy + 150, 400, 16, fb_rgb(40, 40, 60));   /* Progress bar frame */
    fb_fill_rect(cx - 198, cy + 152, 396, 12, fb_rgb(0, 200, 255));  /* Bar fill (full) */
}

/* NEW: Custom pixel drawing and screen helper functions for logo rendering */
void fb_put_pixel(u32 x, u32 y, u32 color)
{
    if (!fb.ready || x >= fb.width || y >= fb.height) return;
    volatile u32 *p = (volatile u32 *)(fb.base + y * fb.pitch) + x;
    *p = color;
}

void fb_fill_screen(u32 color)
{
    if (!fb.ready) return;
    fb_fill_rect(0, 0, fb.width, fb.height, color);
}

u32 fb_get_width(void)  { return fb.ready ? fb.width : 0; }
u32 fb_get_height(void) { return fb.ready ? fb.height : 0; }
