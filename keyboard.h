/* keyboard.h - PS/2 keyboard driver */
#ifndef KEYBOARD_H
#define KEYBOARD_H

void keyboard_init(void);       /* Install the IRQ1 handler and unmask the IRQ */
char keyboard_getchar(void);    /* BLOCK until a key is available, then return its ASCII code.
                                 * Must be called with interrupts enabled (after sti). */

#endif
