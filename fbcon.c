/* fbcon.c - bitmap-font text console on the linear framebuffer.
 * Cooperates with cursor.c: every pixel-writing operation locks the area it modifies,
 * so the mouse cursor's saved background never overwrites fresh text. */

#include "fbcon.h"
#include "fb.h"
#include "cursor.h"
#include "kstring.h"

extern const u8 _binary_font_psf_start[];    /* Created by: objcopy -I binary ... font.psf */

static const u8 *glyphs;
static u32 gw, gh, gbytes, growbytes, gcount;
static u32 cols, rows, cur_col, cur_row;
static u32 fg_rgb, bg_rgb;
static int active = 0;

/* Standard 16-colour VGA palette as 8-bit RGB */
static const u8 pal[16][3] = {
    {0,0,0},{0,0,170},{0,170,0},{0,170,170},{170,0,0},{170,0,170},{170,85,0},{170,170,170},
    {85,85,85},{85,85,255},{85,255,85},{85,255,255},{255,85,85},{255,85,255},{255,255,85},{255,255,255}
};

void fbcon_set_color(u8 a)
{
    fg_rgb = fb_rgb(pal[a & 15][0],        pal[a & 15][1],        pal[a & 15][2]);
    bg_rgb = fb_rgb(pal[(a >> 4) & 15][0], pal[(a >> 4) & 15][1], pal[(a >> 4) & 15][2]);
}

int fbcon_init(void)
{
    const struct framebuffer *f = fb_get();
    if (!f->ready) return -1;

    const u8  *p  = _binary_font_psf_start;
    const u32 *p2 = (const u32 *)p;
    if (p[0] == 0x36 && p[1] == 0x04) {                 /* PSF1: always 8 px wide */
        gw = 8; gh = p[3]; gbytes = gh; growbytes = 1;
        gcount = (p[2] & 1) ? 512 : 256; glyphs = p + 4;
    } else if (p2[0] == 0x864AB572u) {                  /* PSF2 */
        glyphs = p + p2[2]; gcount = p2[4]; gbytes = p2[5]; gh = p2[6]; gw = p2[7];
        growbytes = (gw + 7) / 8;
    } else {
        return -1;                                      /* Not a PSF font */
    }
    cols = f->width / gw;  rows = f->height / gh;
    cur_col = cur_row = 0;
    fbcon_set_color(0x07);
    return 0;
}

int  fbcon_active(void) { return active; }
void fbcon_enable(void) { active = 1; }

/* Draw one glyph into text cell (col, row). The cell's pixel rectangle is locked so the
 * mouse cursor (if it overlaps) is lifted off first. */
static void draw_glyph(u32 col, u32 row, u8 ch, u32 fg, u32 bg)
{
    const struct framebuffer *f = fb_get();
    const u8 *g = glyphs + (u32)(ch < gcount ? ch : '?') * gbytes;

    cursor_lock_area(col * gw, row * gh, gw, gh);
    for (u32 y = 0; y < gh; y++) {
        volatile u32 *dst = (volatile u32 *)(f->base + (row * gh + y) * f->pitch) + col * gw;
        for (u32 x = 0; x < gw; x++) {
            dst[x] = (g[y * growbytes + x / 8] & (0x80 >> (x & 7))) ? fg : bg;
        }
    }
    cursor_unlock();
}

void fbcon_put_at(u32 row, u32 col, char c, u8 a)
{
    if (!active || row >= rows || col >= cols) return;
    draw_glyph(col, row, (u8)c,
               fb_rgb(pal[a & 15][0], pal[a & 15][1], pal[a & 15][2]),
               fb_rgb(pal[(a >> 4) & 15][0], pal[(a >> 4) & 15][1], pal[(a >> 4) & 15][2]));
}

void fbcon_clear(void)
{
    const struct framebuffer *f = fb_get();

    cursor_lock_area(0, 0, f->width, f->height);        /* Whole screen changes */
    fb_fill_rect(0, 0, f->width, f->height, bg_rgb);
    cursor_unlock();

    cur_col = cur_row = 0;
}

static void scroll(void)
{
    const struct framebuffer *f = fb_get();

    cursor_lock_area(0, 0, f->width, f->height);        /* Whole screen moves up: lift the cursor off first */
    memmove(f->base, f->base + f->pitch * gh, f->pitch * gh * (rows - 1));   /* Up one text row */
    fb_fill_rect(0, (rows - 1) * gh, f->width, gh, bg_rgb);                  /* Blank the last row */
    cursor_unlock();

    cur_row = rows - 1;
}

void fbcon_putchar(char c)
{
    if (c == '\n')      { cur_col = 0; cur_row++; }
    else if (c == '\r') { cur_col = 0; }
    else if (c == '\b') {
        if (cur_col > 0) cur_col--; else if (cur_row > 0) { cur_row--; cur_col = cols - 1; } else return;
        draw_glyph(cur_col, cur_row, ' ', fg_rgb, bg_rgb);
    }
    else if (c == '\t') { cur_col = (cur_col + 4) & ~3u; }
    else { draw_glyph(cur_col, cur_row, (u8)c, fg_rgb, bg_rgb); cur_col++; }

    if (cur_col >= cols) { cur_col = 0; cur_row++; }
    if (cur_row >= rows) scroll();
}
