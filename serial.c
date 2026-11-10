/* serial.c - COM1 (0x3F8) debug output. QEMU forwards it with -serial stdio. */
#include "serial.h"
#include "io.h"

#define COM1 0x3F8

void serial_init(void)
{
    outb(COM1 + 1, 0x00);   /* Disable UART interrupts (we poll) */
    outb(COM1 + 3, 0x80);   /* DLAB = 1: expose the baud divisor registers */
    outb(COM1 + 0, 0x03);   /* Divisor 3 -> 38400 baud (low byte) */
    outb(COM1 + 1, 0x00);   /* (high byte) */
    outb(COM1 + 3, 0x03);   /* DLAB = 0, 8 data bits, no parity, 1 stop bit */
    outb(COM1 + 2, 0xC7);   /* Enable and clear FIFOs */
    outb(COM1 + 4, 0x0B);   /* DTR + RTS + OUT2 */
}

void serial_putc(char c)
{
    while (!(inb(COM1 + 5) & 0x20)) { }     /* Wait until the transmit buffer is empty */
    if (c == '\n') {                        /* Terminals want CR+LF */
        outb(COM1, '\r');
        while (!(inb(COM1 + 5) & 0x20)) { }
    }
    outb(COM1, (u8)c);
}

void serial_write(const char *s) { while (*s) serial_putc(*s++); }

void serial_write_hex(u32 v)
{
    const char *d = "0123456789ABCDEF";
    serial_write("0x");
    for (int s = 28; s >= 0; s -= 4) serial_putc(d[(v >> s) & 0xF]);
}
