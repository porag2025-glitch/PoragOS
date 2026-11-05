/* vga.c - VGA text-mode driver (80x25) using memory-mapped I/O at 0xB8000.
 * Each cell is 16 bits: low byte = ASCII, high byte = (background << 4) | foreground. */

#include "vga.h"
#include "io.h"

#define VGA_WIDTH     80                 /* Columns */
#define VGA_HEIGHT    25                 /* Rows */
#define VGA_MEMORY    0xB8000            /* Physical address of the text buffer */
#define VGA_CTRL_PORT 0x3D4              /* CRT controller index register */
#define VGA_DATA_PORT 0x3D5              /* CRT controller data register */

/* volatile: the compiler must perform every write, since the video card reads this memory */
static volatile u16 *const vga_buffer = (volatile u16 *)VGA_MEMORY;
static u32 cursor_row = 0;               /* Current row    (0..24) */
static u32 cursor_col = 0;               /* Current column (0..79) */
static u8  current_color = 0x07;         /* Default: light grey on black */

/* Build one 16-bit cell from a character and a colour attribute */
static inline u16 vga_make_entry(char c, u8 color)
{
    return (u16)((u8)c) | ((u16)color << 8);
}

/* Move the hardware blinking cursor to the current row/column */
static void vga_update_cursor(void)
{
    u16 pos = (u16)(cursor_row * VGA_WIDTH + cursor_col); /* Linear cell index */
    outb(VGA_CTRL_PORT, 0x0F);                            /* Select "cursor low byte" register */
    outb(VGA_DATA_PORT, (u8)(pos & 0xFF));                /* Write low 8 bits */
    outb(VGA_CTRL_PORT, 0x0E);                            /* Select "cursor high byte" register */
    outb(VGA_DATA_PORT, (u8)((pos >> 8) & 0xFF));         /* Write high 8 bits */
}

void terminal_set_color(enum vga_color fg, enum vga_color bg)
{
    current_color = (u8)(fg | (bg << 4));                 /* Low nibble fg, high nibble bg */
}

void terminal_clear(void)
{
    for (u32 i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {    /* Every cell on the screen ... */
        vga_buffer[i] = vga_make_entry(' ', current_color); /* ... becomes a blank space */
    }
    cursor_row = 0;                                       /* Cursor back to top-left */
    cursor_col = 0;
    vga_update_cursor();
}

/* Scroll everything up by one line */
static void terminal_scroll(void)
{
    for (u32 i = 0; i < VGA_WIDTH * (VGA_HEIGHT - 1); i++) {
        vga_buffer[i] = vga_buffer[i + VGA_WIDTH];        /* Copy each row onto the one above */
    }
    for (u32 x = 0; x < VGA_WIDTH; x++) {                 /* Blank the last row */
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_make_entry(' ', current_color);
    }
    cursor_row = VGA_HEIGHT - 1;                          /* Stay on the last row */
}

/* Erase the character before the cursor (used by the Backspace key) */
static void terminal_backspace(void)
{
    if (cursor_col > 0) {                                 /* Normal case: step left */
        cursor_col--;
    } else if (cursor_row > 0) {                          /* At column 0: go to end of previous row */
        cursor_row--;
        cursor_col = VGA_WIDTH - 1;
    } else {
        return;                                           /* Top-left corner: nothing to erase */
    }
    vga_buffer[cursor_row * VGA_WIDTH + cursor_col] = vga_make_entry(' ', current_color);
}

void terminal_putchar(char c)
{
    if (c == '\n') {                                      /* Newline: column 0 of next row */
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {                               /* Carriage return */
        cursor_col = 0;
    } else if (c == '\b') {                               /* Backspace */
        terminal_backspace();
    } else if (c == '\t') {                               /* Tab: advance to next multiple of 4 */
        cursor_col = (cursor_col + 4) & ~3u;
    } else {                                              /* Printable character */
        vga_buffer[cursor_row * VGA_WIDTH + cursor_col] = vga_make_entry(c, current_color);
        cursor_col++;
    }

    if (cursor_col >= VGA_WIDTH) {                        /* Past right edge: wrap */
        cursor_col = 0;
        cursor_row++;
    }
    if (cursor_row >= VGA_HEIGHT) {                       /* Past bottom: scroll */
        terminal_scroll();
    }
    vga_update_cursor();                                  /* Keep hardware cursor in sync */
}

void terminal_write(const char *str)
{
    for (u32 i = 0; str[i] != '\0'; i++) {                /* Walk to the terminating 0 byte */
        terminal_putchar(str[i]);
    }
}

void terminal_write_hex(u32 value)
{
    const char *digits = "0123456789ABCDEF";              /* nibble -> character */
    terminal_write("0x");
    for (int shift = 28; shift >= 0; shift -= 4) {        /* 8 nibbles, most significant first */
        terminal_putchar(digits[(value >> shift) & 0xF]);
    }
}

void terminal_write_dec(u32 value)
{
    char buf[11];                                         /* 4294967295 has 10 digits (+ spare) */
    int  i = 0;                                           /* Number of digits produced so far */

    if (value == 0) {                                     /* Special case: the loop below would print nothing */
        terminal_putchar('0');
        return;
    }
    while (value > 0) {                                   /* Peel off digits, least significant first */
        buf[i++] = (char)('0' + (value % 10));            /* u32 / and % by a constant compile to a multiply: no libgcc */
        value /= 10;
    }
    while (i > 0) {                                       /* Digits came out reversed, so print backwards */
        terminal_putchar(buf[--i]);
    }
}
