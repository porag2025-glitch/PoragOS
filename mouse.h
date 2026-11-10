/* mouse.h - PS/2 mouse driver (8042 controller, auxiliary port, IRQ12) */
#ifndef MOUSE_H
#define MOUSE_H

#include "types.h"          /* u8, u16, u32 */

/* Button bits in mouse_state.buttons */
#define MOUSE_BTN_LEFT   0x01
#define MOUSE_BTN_RIGHT  0x02
#define MOUSE_BTN_MIDDLE 0x04

/* Snapshot of the mouse, filled by mouse_get_state() */
struct mouse_state {
    int x;                  /* Absolute X position, clamped to [0, width-1] */
    int y;                  /* Absolute Y position, clamped to [0, height-1] (0 = top) */
    u8  buttons;            /* OR of MOUSE_BTN_* bits (1 = pressed) */
    u32 packets;            /* Number of valid 3-byte packets received so far */
};

/* Initialise the 8042's mouse port and the mouse itself, then install the IRQ12 handler.
 * Call after idt_init() and pic_remap(), and BEFORE the first `sti`.
 * On failure it logs the reason to the serial port and leaves the mouse disabled. */
void mouse_init(void);

/* 1 if mouse_init() completed successfully, 0 otherwise. */
int  mouse_is_ready(void);

/* Set the area the cursor is confined to (normally the screen size). Default: 1024x768. */
void mouse_set_bounds(u32 width, u32 height);

/* Copy the current position, buttons and packet count into *out (interrupt-safe). */
void mouse_get_state(struct mouse_state *out);

#endif /* MOUSE_H */
