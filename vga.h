/* vga.h - public interface of the VGA text-mode terminal driver */
#ifndef VGA_H
#define VGA_H

#include "types.h"

/* The 16 standard VGA text-mode colours */
enum vga_color {
    VGA_BLACK = 0,  VGA_BLUE = 1,        VGA_GREEN = 2,        VGA_CYAN = 3,
    VGA_RED = 4,    VGA_MAGENTA = 5,     VGA_BROWN = 6,        VGA_LIGHT_GREY = 7,
    VGA_DARK_GREY = 8, VGA_LIGHT_BLUE = 9, VGA_LIGHT_GREEN = 10, VGA_LIGHT_CYAN = 11,
    VGA_LIGHT_RED = 12, VGA_LIGHT_MAGENTA = 13, VGA_LIGHT_BROWN = 14, VGA_WHITE = 15
};

void terminal_set_color(enum vga_color fg, enum vga_color bg); /* Choose colours for later output */
void terminal_clear(void);               /* Blank the screen, cursor to (0,0) */
void terminal_putchar(char c);           /* Print one char (handles \n \r \t \b) */
void terminal_write(const char *str);    /* Print a NUL-terminated string */
void terminal_write_hex(u32 value);      /* Print a 32-bit value as 0xXXXXXXXX */
void terminal_write_dec(u32 value);      /* Print a 32-bit value in decimal */

/* NEW: Write a character at (row, col) with an explicit colour without moving the cursor */
void vga_put_at(u32 row, u32 col, char c, u8 color);

#endif /* VGA_H */
