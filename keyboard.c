/* keyboard.c - PS/2 keyboard driver using scancode set 1.
 *
 * ISR side  : decode scancode -> ASCII, push the ASCII into a ring buffer.
 * Reader side: keyboard_getchar() pops characters out (used by the shell).
 *
 * Scancode reminder:
 *   - Press   sends a MAKE code            (e.g. 'A' -> 0x1E)
 *   - Release sends the BREAK code = make | 0x80 (e.g. 'A' -> 0x9E)
 *   - Extended keys (arrows, ...) send a 0xE0 prefix byte first. */

#include "keyboard.h"
#include "idt.h"
#include "pic.h"
#include "io.h"

#define KBD_DATA_PORT   0x60        /* Read: scancode.  Write: command to the keyboard */
#define KBD_STATUS_PORT 0x64        /* Read: controller status (bit 0 = output buffer full) */
#define KBD_IRQ         1           /* Keyboard is IRQ1 ...                     */
#define KBD_VECTOR      33          /* ... which is vector 0x20 + 1 = 33        */

#define SC_LSHIFT       0x2A        /* Left  Shift make code */
#define SC_RSHIFT       0x36        /* Right Shift make code */
#define SC_CAPSLOCK     0x3A        /* Caps Lock   make code */
#define SC_TABLE_SIZE   0x3A        /* Our tables cover scancodes 0x00-0x39 */

/* ------------------------------------------------------------------------- */
/* Ring buffer: ISR writes at `head`, keyboard_getchar() reads at `tail`.    */
/* Indices are u8 and the buffer has exactly 256 slots, so the indices wrap  */
/* around automatically on overflow (255 + 1 == 0). Cheap and elegant.       */
/* One slot is sacrificed to tell "full" (head+1 == tail) from "empty"       */
/* (head == tail), so the usable capacity is 255 characters.                 */
/* `volatile` because the ISR changes them behind the main code's back.      */
/* ------------------------------------------------------------------------- */
#define KBD_BUF_SIZE 256
static char          kbd_buf[KBD_BUF_SIZE];
static volatile u8   kbd_head = 0;  /* Next slot the ISR will write */
static volatile u8   kbd_tail = 0;  /* Next slot the reader will read */

/* Called from the ISR only. If the buffer is full the key is dropped. */
static void kbd_buffer_push(char c)
{
    u8 next = (u8)(kbd_head + 1);   /* Where head would move to (wraps at 256) */
    if (next == kbd_tail) {         /* Would collide with the reader: buffer full */
        return;                     /* Drop the keystroke rather than overwrite old data */
    }
    kbd_buf[kbd_head] = c;          /* Store the character FIRST ... */
    kbd_head = next;                /* ... then publish it by moving head */
}

/* Scancode -> ASCII, no modifiers. A 0 means "no printable character". */
static const char scancode_normal[SC_TABLE_SIZE] = {
    /* 0x00-0x0E */ 0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    /* 0x0F-0x1C */ '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    /* 0x1D-0x29 */ 0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    /* 0x2A-0x39 */ 0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
};

/* Scancode -> ASCII while Shift is held */
static const char scancode_shifted[SC_TABLE_SIZE] = {
    /* 0x00-0x0E */ 0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    /* 0x0F-0x1C */ '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    /* 0x1D-0x29 */ 0,'A','S','D','F','G','H','J','K','L',':','"','~',
    /* 0x2A-0x39 */ 0,'|','Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
};

/* Modifier state, updated on every key event */
static u8 shift_down = 0;           /* 1 while either Shift key is held */
static u8 caps_on    = 0;           /* Toggled by each Caps Lock press */
static u8 extended   = 0;           /* 1 if the previous byte was the 0xE0 prefix */

/* Runs every time IRQ1 fires (interrupt context: keep it SHORT) */
static void keyboard_handler(registers_t *regs)
{
    (void)regs;                                     /* CPU state is not needed here */

    u8 scancode = inb(KBD_DATA_PORT);               /* Read the byte. This also lets the
                                                     * controller send the next one. */

    if (scancode == 0xE0) {                         /* Extended-key prefix ... */
        extended = 1;                               /* ... remember it, wait for the next byte */
        return;
    }
    if (extended) {                                 /* Second byte of an extended key */
        extended = 0;                               /* (arrows, Delete, ...): ignored for now */
        return;
    }

    u8 released = scancode & 0x80;                  /* Bit 7 set = key RELEASE */
    u8 code     = scancode & 0x7F;                  /* Strip bit 7 to get the key identity */

    if (code == SC_LSHIFT || code == SC_RSHIFT) {   /* Shift: track held/released */
        shift_down = released ? 0 : 1;
        return;
    }
    if (code == SC_CAPSLOCK) {                      /* Caps Lock: toggle on press only */
        if (!released) caps_on ^= 1;
        return;
    }
    if (released || code >= SC_TABLE_SIZE) {        /* Ignore releases and unknown keys */
        return;
    }

    char c = scancode_normal[code];                 /* Start with the unshifted character */
    if (c >= 'a' && c <= 'z') {                     /* Letters: Shift XOR CapsLock => upper-case */
        if (shift_down ^ caps_on) c = scancode_shifted[code];
    } else if (shift_down) {                        /* Non-letters: only Shift matters */
        c = scancode_shifted[code];
    }

    if (c != 0) {                                   /* 0 = Ctrl/Alt/F-keys/etc. */
        kbd_buffer_push(c);                         /* Hand the character to the reader. NO printing here. */
    }
}

char keyboard_getchar(void)
{
    for (;;) {
        __asm__ volatile ("cli" ::: "memory");      /* Interrupts off: head can't change under us */

        if (kbd_head != kbd_tail) {                 /* Data available? */
            char c = kbd_buf[kbd_tail];             /* Take the oldest character */
            kbd_tail = (u8)(kbd_tail + 1);          /* Advance tail (wraps at 256) */
            __asm__ volatile ("sti" ::: "memory");  /* Interrupts back on */
            return c;
        }

        /* Buffer empty: sleep until an interrupt arrives.
         * "sti; hlt" is deliberate. STI only takes effect AFTER the next
         * instruction, so no interrupt can slip in between STI and HLT.
         * If we did "sti" and "hlt" separately, a key could arrive in the gap,
         * and we'd sleep with a character already waiting. */
        __asm__ volatile ("sti; hlt" ::: "memory");
        /* Woken by an interrupt: loop around and re-check the buffer */
    }
}

void keyboard_init(void)
{
    while (inb(KBD_STATUS_PORT) & 0x01) {           /* Drain stale bytes left by firmware/GRUB */
        (void)inb(KBD_DATA_PORT);
    }
    register_interrupt_handler(KBD_VECTOR, keyboard_handler); /* Attach C handler to vector 33 */
    pic_unmask_irq(KBD_IRQ);                        /* Let IRQ1 through the PIC */
}
