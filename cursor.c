/* cursor.c - software mouse cursor with a "save-under" background buffer.
 *
 * Life cycle of one cursor move (all inside cursor_draw):
 *
 *     screen                  backup[]                 action
 *   ┌──────────┐
 *   │ ░░░░░░░░ │  ◄── 1. restore_background()  writes the OLD saved pixels back
 *   │ ░░░░░░░░ │
 *   │ ░░░ ▒▒▒▒ │  ──► 2. save_background()     copies the 12x12 pixels under the NEW
 *   │ ░░░ ▒▒▒▒ │                               position into backup[]
 *   │ ░░░ ▒▒▒▒ │  ◄── 3. draw_shape()          paints the arrow over them
 *   └──────────┘
 *
 * Because step 1 puts the original pixels back, the cursor never permanently damages
 * whatever is on the screen.
 *
 * Pixel layout reminder: the framebuffer is 32 bits per pixel and rows are `pitch` BYTES
 * apart (pitch can be larger than width*4), so a pixel address is
 *     base + y * pitch + x * 4. */

#include "cursor.h"
#include "mouse.h"
#include "fb.h"
#include "serial.h"

/* ---- Sprite ---------------------------------------------------------------
 * Each character is one pixel:
 *     'X' = outline colour (black)
 *     'W' = fill colour (white; changes while a mouse button is held)
 *     '.' = transparent (the background stays visible)
 * The hotspot, the pixel that "points" at things, is the top-left tip, (0,0).
 * Each row has exactly 12 visible characters + the terminating NUL. */
static const char cursor_shape[CURSOR_HEIGHT][CURSOR_WIDTH + 1] = {
    "X...........",
    "XX..........",
    "XWX.........",
    "XWWX........",
    "XWWWX.......",
    "XWWWWX......",
    "XWWWWWX.....",
    "XWWWWWWX....",
    "XWWWWXXXX...",
    "XWWXWWX.....",
    "XWX.XWWX....",
    "XX...XWWX..."
};

/* ---- State ---------------------------------------------------------------- */
static u32 backup[CURSOR_WIDTH * CURSOR_HEIGHT];    /* Saved pixels; pixel (col,row) is at [row*12 + col] */

static int initialized = 0;         /* cursor_init() succeeded */
static int visible     = 1;         /* User-level show/hide switch */
static int drawn       = 0;         /* 1 = the arrow is on screen and backup[] holds what is under it */
static u32 lock_depth  = 0;         /* >0 while somebody else is drawing on the framebuffer */

/* Where the arrow currently is on screen, and the clipped part that was saved */
static int drawn_x = 0, drawn_y = 0;
static u8  drawn_buttons = 0;
static u32 saved_x = 0, saved_y = 0;    /* Top-left of the saved rectangle (screen pixels) */
static u32 saved_w = 0, saved_h = 0;    /* Its size after clipping to the screen (<= 12) */

/* Colours, packed for this framebuffer's pixel format in cursor_init() */
static u32 col_outline;
static u32 col_fill_normal, col_fill_left, col_fill_right, col_fill_middle;

/* ---- Interrupt-safe critical sections (same pattern as heap.c) -------------- */
static inline u32 irq_save(void)
{
    u32 flags;
    __asm__ volatile ("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(u32 flags)
{
    if (flags & 0x200u) {                       /* EFLAGS.IF: only re-enable if it was enabled */
        __asm__ volatile ("sti" : : : "memory");
    }
}

/* ---- Pixel access ------------------------------------------------------------ */

/* Address of pixel (x, y). The caller guarantees x < width and y < height.
 * `volatile`: this is video memory, so every access must really happen. */
static inline volatile u32 *pixel_ptr(const struct framebuffer *f, u32 x, u32 y)
{
    return (volatile u32 *)(f->base + y * f->pitch) + x;    /* pitch is in BYTES: add it to a byte pointer */
}

/* Step 2: copy the screen pixels under a cursor at (x, y) into backup[].
 * Only the part that lies inside the screen is saved: that is the safety check
 * that stops us from reading outside the framebuffer. Requires x < width, y < height. */
static void save_background(const struct framebuffer *f, u32 x, u32 y)
{
    saved_x = x;
    saved_y = y;
    saved_w = (x + CURSOR_WIDTH  > f->width)  ? f->width  - x : CURSOR_WIDTH;    /* Clip at the right edge */
    saved_h = (y + CURSOR_HEIGHT > f->height) ? f->height - y : CURSOR_HEIGHT;   /* Clip at the bottom edge */

    for (u32 row = 0; row < saved_h; row++) {
        for (u32 col = 0; col < saved_w; col++) {
            backup[row * CURSOR_WIDTH + col] = *pixel_ptr(f, x + col, y + row);
        }
    }
}

/* Step 1: put the saved pixels back where they came from. */
static void restore_background(const struct framebuffer *f)
{
    if (!drawn) return;                         /* Nothing of ours is on screen */

    for (u32 row = 0; row < saved_h; row++) {
        for (u32 col = 0; col < saved_w; col++) {
            *pixel_ptr(f, saved_x + col, saved_y + row) = backup[row * CURSOR_WIDTH + col];
        }
    }
    drawn = 0;
}

/* Step 3: paint the arrow over the (already saved) area. Only pixels inside the saved,
 * i.e. clipped, rectangle are touched. */
static void draw_shape(const struct framebuffer *f, u32 x, u32 y, u8 buttons)
{
    u32 fill = col_fill_normal;                 /* Pick the fill colour from the button state */
    if      (buttons & MOUSE_BTN_LEFT)   fill = col_fill_left;
    else if (buttons & MOUSE_BTN_RIGHT)  fill = col_fill_right;
    else if (buttons & MOUSE_BTN_MIDDLE) fill = col_fill_middle;

    for (u32 row = 0; row < saved_h; row++) {
        for (u32 col = 0; col < saved_w; col++) {
            char s = cursor_shape[row][col];
            if (s == 'X')      *pixel_ptr(f, x + col, y + row) = col_outline;
            else if (s == 'W') *pixel_ptr(f, x + col, y + row) = fill;
            /* '.' = transparent: leave the pixel alone */
        }
    }
}

/* ---- Public API ---------------------------------------------------------------- */

void cursor_init(void)
{
    const struct framebuffer *f = fb_get();

    initialized = 0;                            /* Stay disabled unless everything below checks out */
    drawn       = 0;
    lock_depth  = 0;

    if (!f->ready) {
        serial_write("[cursor] disabled: no framebuffer\n");
        return;
    }
    if (!mouse_is_ready()) {
        serial_write("[cursor] disabled: mouse not initialised\n");
        return;
    }

    /* Colours are packed according to the pixel format GRUB reported (usually 0x00RRGGBB). */
    col_outline     = fb_rgb(0,   0,   0);
    col_fill_normal = fb_rgb(255, 255, 255);
    col_fill_left   = fb_rgb(255, 220, 0);      /* Yellow while the left button is held */
    col_fill_right  = fb_rgb(0,   200, 255);    /* Cyan   while the right button is held */
    col_fill_middle = fb_rgb(80,  255, 80);     /* Green  while the middle button is held */

    mouse_set_bounds(f->width, f->height);      /* The mouse can never leave the screen */

    initialized = 1;
    visible     = 1;

    serial_write("[cursor] ready, screen ");
    serial_write_hex(f->width);
    serial_write(" x ");
    serial_write_hex(f->height);
    serial_write("\n");

    cursor_draw();                              /* Show it right away */
}

void cursor_draw(void)
{
    if (!initialized) return;

    const struct framebuffer *f = fb_get();

    struct mouse_state m;
    mouse_get_state(&m);                        /* Interrupt-safe snapshot of x, y, buttons */

    /* Safety check #1: never use a position outside the screen, even if the mouse driver
     * has different bounds (for example if they were set before a mode change). */
    int x = m.x;
    int y = m.y;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (int)f->width)  x = (int)f->width  - 1;
    if (y >= (int)f->height) y = (int)f->height - 1;

    u32 flags = irq_save();                     /* From here on nothing else may touch the framebuffer
                                                 * or our state (the shell, the HUD or a task switch) */

    if (lock_depth > 0 || !visible) {           /* Someone is drawing text, or the cursor is hidden */
        irq_restore(flags);
        return;
    }

    if (drawn && x == drawn_x && y == drawn_y && m.buttons == drawn_buttons) {
        irq_restore(flags);                     /* Nothing changed: the common case when idle */
        return;
    }

    restore_background(f);                      /* 1. Put the old pixels back (no-op if not drawn) */
    save_background(f, (u32)x, (u32)y);         /* 2. Remember the pixels under the new spot */
    draw_shape(f, (u32)x, (u32)y, m.buttons);   /* 3. Paint the arrow */

    drawn         = 1;
    drawn_x       = x;
    drawn_y       = y;
    drawn_buttons = m.buttons;

    irq_restore(flags);
}

void cursor_set_visible(int v)
{
    if (!initialized) return;

    u32 flags = irq_save();
    visible = v ? 1 : 0;
    if (!visible) {
        restore_background(fb_get());           /* Remove the arrow from the screen immediately */
    }
    irq_restore(flags);                         /* When made visible again, the next cursor_draw() paints it */
}

int cursor_is_visible(void)
{
    return initialized && visible;
}

void cursor_lock_area(u32 x, u32 y, u32 w, u32 h)
{
    u32 flags = irq_save();

    if (initialized) {
        lock_depth++;                           /* From now on cursor_draw() does nothing */

        if (drawn) {
            /* Rectangle intersection test: the two rectangles overlap if, on BOTH axes,
             * each one starts before the other one ends. */
            int overlap = (x < saved_x + saved_w) && (saved_x < x + w) &&
                          (y < saved_y + saved_h) && (saved_y < y + h);
            if (overlap) {
                restore_background(fb_get());   /* Caller's pixels will land on a clean background */
            }
        }
    }

    irq_restore(flags);
}

void cursor_unlock(void)
{
    u32 flags = irq_save();

    if (lock_depth > 0) {                       /* Guarded: a lock taken before cursor_init() was never counted */
        lock_depth--;
    }
    /* No redraw here on purpose: the cursor task repaints within a few ms. This keeps heavy
     * text output fast and avoids flicker. */

    irq_restore(flags);
}
