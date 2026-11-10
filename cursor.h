/* cursor.h - software mouse cursor on the VESA linear framebuffer */
#ifndef CURSOR_H
#define CURSOR_H

#include "types.h"

#define CURSOR_WIDTH   12       /* Sprite size in pixels */
#define CURSOR_HEIGHT  12

/* Prepare the cursor. Requires a ready framebuffer (fb_init OK) and a working mouse
 * (mouse_init OK). If either is missing it logs the reason to COM1 and stays disabled,
 * so every other cursor_* function is then a harmless no-op.
 * Also confines the mouse to the framebuffer size. */
void cursor_init(void);

/* Update the on-screen cursor. Call regularly (a background task every 10-20 ms):
 *   - nothing changed since the last call -> returns immediately
 *   - otherwise: restore the old background, save the pixels under the new position,
 *     draw the arrow there.
 * Does nothing while the cursor is hidden or locked by a drawing operation. */
void cursor_draw(void);

/* Show or hide the cursor (hiding restores the background at once). Default: visible. */
void cursor_set_visible(int visible);
int  cursor_is_visible(void);

/* Handshake for code that draws on the framebuffer (fbcon, splash, ...):
 *   cursor_lock_area(x, y, w, h);   // pixel rectangle you are about to modify
 *   ... draw ...
 *   cursor_unlock();
 * If the cursor overlaps the rectangle its background is restored first, so your pixels
 * are never overwritten by a stale backup. While locked, cursor_draw() does nothing.
 * Calls may nest; every lock needs exactly one unlock. */
void cursor_lock_area(u32 x, u32 y, u32 w, u32 h);
void cursor_unlock(void);

#endif /* CURSOR_H */
