/* pit.h - 8253/8254 Programmable Interval Timer driver (Channel 0 -> IRQ0) */
#ifndef PIT_H
#define PIT_H

#include "types.h"

#define PIT_DEFAULT_HZ 100u             /* 100 ticks/sec -> 10 ms per tick */

void pit_init(u32 frequency_hz);        /* Program Channel 0 and unmask IRQ0 */
u32  pit_get_ticks(void);               /* Monotonic tick count since pit_init() */
u32  pit_get_frequency(void);           /* Configured tick rate in Hz */
u32  pit_ms_to_ticks(u32 ms);           /* Convert milliseconds to ticks (at least 1) */

#endif /* PIT_H */
