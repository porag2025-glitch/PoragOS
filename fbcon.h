/* fbcon.h - Framebuffer text console header */
#ifndef FBCON_H
#define FBCON_H

#include "types.h"

int  fbcon_init(void);
void fbcon_enable(void);
int  fbcon_active(void);
void fbcon_set_color(u8 color);
void fbcon_clear(void);
void fbcon_putchar(char c);
void fbcon_put_at(u32 row, u32 col, char c, u8 color);

#endif /* FBCON_H */
