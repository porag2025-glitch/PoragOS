/* pic.c - two cascaded 8259 PICs. The master handles IRQ0-7, the slave IRQ8-15
 * and is wired into the master's IRQ2 input. */

#include "pic.h"
#include "io.h"

#define PIC1_CMD   0x20         /* Master command port */
#define PIC1_DATA  0x21         /* Master data port (also the interrupt mask register) */
#define PIC2_CMD   0xA0         /* Slave command port */
#define PIC2_DATA  0xA1         /* Slave data port */

#define PIC_EOI    0x20         /* "End Of Interrupt" command */
#define ICW1_INIT  0x10         /* Begin initialisation sequence */
#define ICW1_ICW4  0x01         /* ICW4 will be sent */
#define ICW4_8086  0x01         /* 8086/88 mode (not the old MCS-80 mode) */

void pic_remap(u8 master_offset, u8 slave_offset)
{
    /* ICW1: start the init sequence. Each PIC now expects 3 more bytes on its data port. */
    outb(PIC1_CMD, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2_CMD, ICW1_INIT | ICW1_ICW4); io_wait();

    /* ICW2: the vector offset, i.e. which IDT entry IRQ0 of each chip will use */
    outb(PIC1_DATA, master_offset);        io_wait();   /* Master: IRQ0-7  -> master_offset.. */
    outb(PIC2_DATA, slave_offset);         io_wait();   /* Slave : IRQ8-15 -> slave_offset.. */

    /* ICW3: describe the cascade wiring */
    outb(PIC1_DATA, 0x04);                 io_wait();   /* Master: slave is on IRQ2 (bit 2 set) */
    outb(PIC2_DATA, 0x02);                 io_wait();   /* Slave : its cascade identity is 2 */

    /* ICW4: operating mode */
    outb(PIC1_DATA, ICW4_8086);            io_wait();
    outb(PIC2_DATA, ICW4_8086);            io_wait();

    /* Mask (disable) all IRQs. Drivers unmask only what they need. */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void pic_send_eoi(u8 irq)
{
    if (irq >= 8) {                         /* IRQ came from the slave ... */
        outb(PIC2_CMD, PIC_EOI);            /* ... so the slave must be told */
    }
    outb(PIC1_CMD, PIC_EOI);                /* The master is ALWAYS told (it relayed the slave) */
}

void pic_unmask_irq(u8 irq)
{
    u16 port;
    if (irq < 8) {
        port = PIC1_DATA;                   /* Master mask register */
    } else {
        port = PIC2_DATA;                   /* Slave mask register */
        irq -= 8;                           /* Bit number within the slave */
        pic_unmask_irq(2);                  /* The cascade line must be open or no slave IRQ arrives */
    }
    outb(port, (u8)(inb(port) & ~(1u << irq)));  /* Clear the bit: 0 = enabled */
}

void pic_mask_irq(u8 irq)
{
    u16 port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    if (irq >= 8) irq -= 8;
    outb(port, (u8)(inb(port) | (1u << irq)));   /* Set the bit: 1 = disabled */
}
