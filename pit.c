/* pit.c - 8253/8254 Programmable Interval Timer driver.
 *
 * Ports:
 *   0x40 : Channel 0 data port (wired to PIC IRQ0 -> IDT vector 32)
 *   0x43 : Mode/Command register
 *
 * Command byte 0x36 (00 11 011 0b):
 *   bits 7-6 = 00  : Select Channel 0
 *   bits 5-4 = 11  : Access mode: lobyte then hibyte
 *   bits 3-1 = 011 : Mode 3 (Square Wave Generator)
 *   bit  0   = 0   : 16-bit binary counter */

#include "pit.h"
#include "idt.h"
#include "pic.h"
#include "io.h"
#include "task.h"

#define PIT_CH0_DATA    0x40            /* Channel 0 data port */
#define PIT_CMD_PORT    0x43            /* Mode/Command register */
#define PIT_BASE_FREQ   1193182u        /* Hardware crystal frequency in Hz */
#define PIT_CMD_MODE3   0x36            /* Ch0, lobyte/hibyte, square wave, binary */

#define PIT_IRQ         0               /* IRQ0 on the master PIC */
#define PIT_VECTOR      32              /* Remapped to IDT entry 32 (0x20) */

static volatile u32 pit_ticks = 0;      /* Incremented on every IRQ0 */
static u32          pit_freq  = PIT_DEFAULT_HZ;

/* IRQ0 Interrupt Service Routine */
static void pit_handler(registers_t *regs)
{
    (void)regs;
    pit_ticks++;                        /* Advance system clock */
    task_tick();                        /* Notify the scheduler (may context-switch!) */
}

void pit_init(u32 frequency_hz)
{
    if (frequency_hz < 19u) frequency_hz = 19u;         /* 1193182 / 65535 = 18.2 Hz min */
    if (frequency_hz > PIT_BASE_FREQ) frequency_hz = PIT_BASE_FREQ;

    pit_freq  = frequency_hz;
    pit_ticks = 0;

    u32 divisor = PIT_BASE_FREQ / frequency_hz;         /* 1193182 / 100 = 11931 */
    if (divisor > 65535u) divisor = 65535u;

    outb(PIT_CMD_PORT, PIT_CMD_MODE3);                  /* Tell PIT we are sending a 16-bit divisor */
    outb(PIT_CH0_DATA, (u8)(divisor & 0xFFu));          /* Low byte first */
    outb(PIT_CH0_DATA, (u8)((divisor >> 8) & 0xFFu));   /* High byte second */

    register_interrupt_handler(PIT_VECTOR, pit_handler);
    pic_unmask_irq(PIT_IRQ);                            /* Enable IRQ0 on the master PIC */
}

u32 pit_get_ticks(void)
{
    return pit_ticks;
}

u32 pit_get_frequency(void)
{
    return pit_freq;
}

u32 pit_ms_to_ticks(u32 ms)
{
    u32 t = (ms * pit_freq + 999u) / 1000u;             /* Round up so sleep(1) waits >= 1 tick */
    return (t == 0) ? 1u : t;
}
