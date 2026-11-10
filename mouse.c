/* mouse.c - PS/2 mouse driver.
 *
 * Hardware background
 * -------------------
 * The 8042 PS/2 controller has two ports to the outside world:
 *     port 1 = keyboard,   port 2 ("auxiliary") = mouse.
 * The CPU talks to the controller through two I/O ports:
 *     0x60 = DATA    : read = byte from device, write = byte to device / command argument
 *     0x64 = STATUS (read)  /  COMMAND (write)
 *
 * Status register (read 0x64):
 *     bit 0 (0x01) Output buffer full : a byte is waiting for us at 0x60
 *     bit 1 (0x02) Input  buffer full : controller still busy, do NOT write yet
 *     bit 5 (0x20) AUX                : the waiting byte came from the MOUSE, not the keyboard
 *
 * Mouse packet (3 bytes, standard PS/2 mouse, sent after "enable data reporting"):
 *     byte 0: bit0 Left  bit1 Right  bit2 Middle  bit3 ALWAYS 1  bit4 X sign  bit5 Y sign
 *             bit6 X overflow  bit7 Y overflow
 *     byte 1: X movement (low 8 bits, two's complement with the sign in byte 0 bit 4)
 *     byte 2: Y movement (low 8 bits, sign in byte 0 bit 5, positive = UP) */

#include "mouse.h"
#include "io.h"             /* inb / outb */
#include "serial.h"         /* serial_write / serial_write_hex */
#include "idt.h"            /* registers_t, register_interrupt_handler */
#include "pic.h"            /* pic_unmask_irq */

/* ---- 8042 ports and bits ----------------------------------------------- */
#define PS2_DATA_PORT      0x60     /* Data register */
#define PS2_STATUS_PORT    0x64     /* Status register (read) */
#define PS2_COMMAND_PORT   0x64     /* Command register (write) */

#define PS2_STATUS_OUT_FULL 0x01    /* A byte is waiting at 0x60 */
#define PS2_STATUS_IN_FULL  0x02    /* Controller input buffer still occupied */
#define PS2_STATUS_AUX      0x20    /* Waiting byte belongs to the mouse */

/* ---- 8042 controller commands (written to 0x64) ------------------------- */
#define PS2_CMD_READ_CONFIG  0x20   /* Next byte readable at 0x60 = configuration byte */
#define PS2_CMD_WRITE_CONFIG 0x60   /* Next byte written to 0x60 = new configuration byte */
#define PS2_CMD_ENABLE_AUX   0xA8   /* Enable the second (mouse) port */
#define PS2_CMD_WRITE_AUX    0xD4   /* Send the next byte written to 0x60 to the MOUSE */

/* ---- Configuration byte bits -------------------------------------------- */
#define PS2_CFG_AUX_IRQ      0x02   /* bit 1: raise IRQ12 when the mouse has data */
#define PS2_CFG_AUX_CLOCK_OFF 0x20  /* bit 5: 1 = mouse clock DISABLED (we clear it) */

/* ---- Mouse commands and replies ----------------------------------------- */
#define MOUSE_CMD_ENABLE_REPORTING 0xF4  /* Start streaming movement packets */
#define MOUSE_REPLY_ACK            0xFA  /* "Command accepted" */

/* ---- Interrupt wiring ---------------------------------------------------- */
#define MOUSE_IRQ     12            /* Mouse is IRQ12 (slave PIC, line 4) */
#define MOUSE_VECTOR  44            /* 0x28 (slave offset) + 4 = 44 after pic_remap(0x20,0x28) */

#define PS2_WAIT_LOOPS 100000       /* Polling budget before we give up (no hardware = no hang) */

/* ---- Driver state -------------------------------------------------------- */
static volatile int  mouse_ready   = 0;     /* Set when initialisation succeeded */
static volatile int  mouse_x       = 0;     /* Cursor position */
static volatile int  mouse_y       = 0;
static volatile u8   mouse_buttons = 0;
static volatile u32  mouse_packets = 0;
static volatile int  bound_w       = 1024;  /* Clamp area */
static volatile int  bound_h       = 768;

static u8 packet[3];                        /* Bytes of the packet being assembled */
static u8 packet_index = 0;                 /* Which byte of the packet comes next (0..2) */

/* ---- Interrupt-safe critical sections (same pattern as the heap) ---------- */
static inline u32 irq_save(void)
{
    u32 flags;
    __asm__ volatile ("pushf\n\tpop %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(u32 flags)
{
    if (flags & 0x200u) {                   /* Only re-enable if interrupts were on before */
        __asm__ volatile ("sti" : : : "memory");
    }
}

/* ---- Low-level 8042 access ------------------------------------------------ */

/* Wait until the controller can ACCEPT a byte (input buffer empty).
 * Required before EVERY write to port 0x60 or 0x64. Returns 0 on success, -1 on timeout. */
static int ps2_wait_write(void)
{
    for (u32 i = 0; i < PS2_WAIT_LOOPS; i++) {
        if (!(inb(PS2_STATUS_PORT) & PS2_STATUS_IN_FULL)) {
            return 0;
        }
    }
    return -1;
}

/* Wait until a byte is AVAILABLE to read at port 0x60 (output buffer full).
 * Returns 0 on success, -1 on timeout. */
static int ps2_wait_read(void)
{
    for (u32 i = 0; i < PS2_WAIT_LOOPS; i++) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUT_FULL) {
            return 0;
        }
    }
    return -1;
}

/* Send a command byte to the controller (port 0x64). Returns 0 or -1. */
static int ps2_send_command(u8 cmd)
{
    if (ps2_wait_write() != 0) return -1;
    outb(PS2_COMMAND_PORT, cmd);
    return 0;
}

/* Send a data byte to the controller / device (port 0x60). Returns 0 or -1. */
static int ps2_send_data(u8 data)
{
    if (ps2_wait_write() != 0) return -1;
    outb(PS2_DATA_PORT, data);
    return 0;
}

/* Read a byte from port 0x60, or return -1 on timeout. */
static int ps2_read_data(void)
{
    if (ps2_wait_read() != 0) return -1;
    return (int)inb(PS2_DATA_PORT);
}

/* ---- Packet decoding -------------------------------------------------------- */

static void mouse_clamp(void)
{
    if (mouse_x < 0)        mouse_x = 0;
    if (mouse_x >= bound_w) mouse_x = bound_w - 1;
    if (mouse_y < 0)        mouse_y = 0;
    if (mouse_y >= bound_h) mouse_y = bound_h - 1;
}

/* Called once a full 3-byte packet is in packet[]. */
static void mouse_process_packet(void)
{
    u8 flags = packet[0];

    mouse_buttons = flags & 0x07;                       /* Bits 0-2: left / right / middle */

    if (!(flags & 0xC0)) {                              /* Bits 6/7 = overflow: movement is garbage, skip it */
        /* 9-bit two's complement: the sign bit lives in byte 0 (bit 4 for X, bit 5 for Y).
         * (flags << 4) & 0x100 turns bit 4 into 0x100, so subtracting it sign-extends the byte. */
        int dx = (int)packet[1] - (int)(((u32)flags << 4) & 0x100u);
        int dy = (int)packet[2] - (int)(((u32)flags << 3) & 0x100u);

        mouse_x += dx;
        mouse_y -= dy;                                  /* PS/2 Y grows UPWARD, screen Y grows DOWNWARD */
        mouse_clamp();
    }

    mouse_packets++;

    if (mouse_packets <= 5) {                           /* Log only the first few: serial output is slow */
        serial_write("[mouse] packet ");
        serial_write_hex(((u32)packet[0] << 16) | ((u32)packet[1] << 8) | packet[2]);
        serial_write(" -> x=");
        serial_write_hex((u32)mouse_x);
        serial_write(" y=");
        serial_write_hex((u32)mouse_y);
        serial_write(" btn=");
        serial_write_hex(mouse_buttons);
        serial_write("\n");
    }
}

/* Feed one byte from the mouse into the 3-byte packet state machine. */
static void mouse_process_byte(u8 data)
{
    if (packet_index == 0 && !(data & 0x08)) {
        return;                                         /* Byte 0 always has bit 3 set: otherwise we are
                                                         * out of sync, so drop bytes until one matches. */
    }
    packet[packet_index++] = data;
    if (packet_index == 3) {
        packet_index = 0;
        mouse_process_packet();
    }
}

/* IRQ12 handler. Runs with interrupts off; the PIC EOI was already sent by isr_handler(). */
static void mouse_handler(registers_t *regs)
{
    (void)regs;

    for (;;) {
        u8 status = inb(PS2_STATUS_PORT);
        if (!(status & PS2_STATUS_OUT_FULL)) break;     /* Nothing (more) to read */
        if (!(status & PS2_STATUS_AUX))      break;     /* Byte belongs to the keyboard: leave it for IRQ1 */
        mouse_process_byte(inb(PS2_DATA_PORT));         /* Reading 0x60 empties the buffer and lets the
                                                         * controller deliver the next byte. */
    }
}

/* ---- Public API ------------------------------------------------------------- */

void mouse_init(void)
{
    int reply;
    u8  config;

    mouse_ready   = 0;
    packet_index  = 0;
    mouse_packets = 0;
    mouse_buttons = 0;
    mouse_x       = bound_w / 2;                        /* Start in the middle of the screen */
    mouse_y       = bound_h / 2;

    serial_write("[mouse] init: start\n");

    /* Throw away stale bytes left by firmware/GRUB so they do not confuse the ACK check below. */
    while (inb(PS2_STATUS_PORT) & PS2_STATUS_OUT_FULL) {
        (void)inb(PS2_DATA_PORT);
    }

    /* Step 1: enable the auxiliary (mouse) port. wait-for-clear is inside ps2_send_command(). */
    if (ps2_send_command(PS2_CMD_ENABLE_AUX) != 0) {
        serial_write("[mouse] FAIL: timeout sending 0xA8 (enable aux port)\n");
        return;
    }

    /* Step 2: read-modify-write the controller configuration byte. */
    if (ps2_send_command(PS2_CMD_READ_CONFIG) != 0) {
        serial_write("[mouse] FAIL: timeout sending 0x20 (read config)\n");
        return;
    }
    reply = ps2_read_data();
    if (reply < 0) {
        serial_write("[mouse] FAIL: timeout reading config byte\n");
        return;
    }
    config = (u8)reply;
    serial_write("[mouse] config byte before: ");
    serial_write_hex(config);
    serial_write("\n");

    config |= PS2_CFG_AUX_IRQ;                          /* Bit 1: enable IRQ12 */
    config &= (u8)~PS2_CFG_AUX_CLOCK_OFF;               /* Bit 5: make sure the mouse clock is running */

    if (ps2_send_command(PS2_CMD_WRITE_CONFIG) != 0 || ps2_send_data(config) != 0) {
        serial_write("[mouse] FAIL: timeout writing config byte\n");
        return;
    }
    serial_write("[mouse] config byte after:  ");
    serial_write_hex(config);
    serial_write("\n");

    /* Step 3: tell the MOUSE (not the keyboard) to start streaming: 0xD4, then 0xF4. */
    if (ps2_send_command(PS2_CMD_WRITE_AUX) != 0 ||
        ps2_send_data(MOUSE_CMD_ENABLE_REPORTING) != 0) {
        serial_write("[mouse] FAIL: timeout sending 0xF4 (enable reporting)\n");
        return;
    }

    reply = ps2_read_data();                            /* The mouse must answer 0xFA = ACK */
    if (reply != MOUSE_REPLY_ACK) {
        serial_write("[mouse] FAIL: expected ACK 0xFA, got ");
        serial_write_hex((u32)reply);                   /* 0xFFFFFFFF means timeout */
        serial_write("\n");
        return;
    }

    /* Step 4: only now hook up the interrupt, so the handler cannot steal the ACK byte. */
    register_interrupt_handler(MOUSE_VECTOR, mouse_handler);
    pic_unmask_irq(MOUSE_IRQ);                          /* Also unmasks the IRQ2 cascade line */

    mouse_ready = 1;
    serial_write("[mouse] init: OK (IRQ12 -> vector 44)\n");
}

int mouse_is_ready(void)
{
    return mouse_ready;
}

void mouse_set_bounds(u32 width, u32 height)
{
    if (width == 0 || height == 0) return;
    u32 flags = irq_save();                             /* The ISR also touches these variables */
    bound_w = (int)width;
    bound_h = (int)height;
    mouse_clamp();
    irq_restore(flags);
}

void mouse_get_state(struct mouse_state *out)
{
    if (out == NULL) return;
    u32 flags = irq_save();                             /* Take a consistent snapshot */
    out->x       = mouse_x;
    out->y       = mouse_y;
    out->buttons = mouse_buttons;
    out->packets = mouse_packets;
    irq_restore(flags);
}
