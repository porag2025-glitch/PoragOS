/* fb.h - Linear framebuffer header */
#ifndef FB_H
#define FB_H
#include "types.h"
#include "multiboot.h"

struct framebuffer {
    u8  *base;                  /* Virtual address of pixel (0,0) */
    u32  width, height, pitch;  /* pitch = BYTES per row */
    u32  bpp;
    u8   red_pos, red_size, green_pos, green_size, blue_pos, blue_size;
    int  ready;
};

int  fb_init(const struct multiboot_info *mbi);   /* 0 = graphics ready, -1 = stay in text mode */
const struct framebuffer *fb_get(void);
u32  fb_rgb(u8 r, u8 g, u8 b);                    /* Pack a colour for this framebuffer's format */
void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color);
void fb_splash(void);

/* NEW: Custom pixel drawing and screen helper functions for logo rendering */
void fb_put_pixel(u32 x, u32 y, u32 color);
void fb_fill_screen(u32 color);
u32  fb_get_width(void);
u32  fb_get_height(void);

#endif /* FB_H */
